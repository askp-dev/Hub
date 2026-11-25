import type { Db } from "./db";
import { config, type Config, type Env } from "./env";
import type { Bucket } from "./storage";
import { CATEGORIES, Invalid, LICENSES, OFFERS, slugify, validateSubmission } from "./validate";

export interface Deps {
  db: Db;
  bucket: Bucket;
  env: Partial<Env>;
  now: () => number;
  /** Outgoing requests (source link check, Discord). Tests stub it. */
  fetcher?: typeof fetch;
}

const DAY = 86_400_000;
const STATUSES = ["uploading", "pending", "in_review", "changes_requested", "approved", "rejected", "withdrawn"] as const;
const REVIEW_STATUSES = ["in_review", "changes_requested", "approved", "rejected"];

const json = (data: unknown, status = 200) =>
  new Response(JSON.stringify(data), { status, headers: { "content-type": "application/json; charset=utf-8", "cache-control": "no-store" } });
const fail = (status: number, error: string, field?: string) => json({ error, ...(field ? { field } : {}) }, status);

const enc = new TextEncoder();
async function sha256hex(s: string) {
  const d = await crypto.subtle.digest("SHA-256", enc.encode(s));
  return [...new Uint8Array(d)].map((b) => b.toString(16).padStart(2, "0")).join("");
}
function randomString(alphabet: string, n: number) {
  const bytes = crypto.getRandomValues(new Uint8Array(n));
  return [...bytes].map((b) => alphabet[b % alphabet.length]).join("");
}
const b64url = (bytes: Uint8Array) => btoa(String.fromCharCode(...bytes)).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
// Crockford base32 without I, L, O, U: easy to read out over email.
const REF_ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

/** Constant-time compare for the admin token. */
function sameSecret(a: string, b: string) {
  if (a.length !== b.length) return false;
  let r = 0;
  for (let i = 0; i < a.length; i++) r |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return r === 0;
}
const bearer = (req: Request) => (req.headers.get("authorization") ?? "").replace(/^Bearer\s+/i, "").trim();

interface SubmissionRow {
  id: string; ref: string; token_hash: string; status: string; plugin_name: string; slug: string; creator_name: string;
  email: string; version: string; tagline: string; description: string; category: string; source_url: string;
  homepage: string | null; license: string; offer: string; price_usd: number | null; tested_in: string | null;
  notes: string | null; platforms: string; formats: string; terms_version: string; hub_version: string | null;
  ip_hash: string; source_status: number | null; review_note: string | null; created_at: number;
  submitted_at: number | null; updated_at: number; reviewed_at: number | null;
}
interface FileRow {
  submission_id: string; name: string; kind: string; size: number; sha256: string; content_type: string;
  bundles: string | null; r2_key: string; upload_id: string | null; done: number; deleted: number;
}

export const partCount = (size: number, partSize: number) => Math.max(1, Math.ceil(size / partSize));
export const partLength = (size: number, partSize: number, n: number) =>
  n < partCount(size, partSize) ? partSize : size - partSize * (partCount(size, partSize) - 1);

function publicView(s: SubmissionRow) {
  return {
    id: s.id, ref: s.ref, status: s.status, pluginName: s.plugin_name, version: s.version,
    reviewNote: s.review_note, submittedAt: s.submitted_at, updatedAt: s.updated_at,
  };
}
function adminView(s: SubmissionRow, files: FileRow[]) {
  return {
    ...publicView(s),
    slug: s.slug, creator: s.creator_name, email: s.email, tagline: s.tagline, description: s.description,
    category: s.category, sourceUrl: s.source_url, sourceStatus: s.source_status, homepage: s.homepage, license: s.license,
    offer: s.offer, priceUsd: s.price_usd, testedIn: s.tested_in, notes: s.notes, platforms: JSON.parse(s.platforms),
    formats: JSON.parse(s.formats), termsVersion: s.terms_version, hubVersion: s.hub_version, createdAt: s.created_at,
    reviewedAt: s.reviewed_at,
    files: files.map((f) => ({
      name: f.name, kind: f.kind, size: f.size, sha256: f.sha256, bundles: f.bundles ? JSON.parse(f.bundles) : null,
      uploaded: !!f.done, deleted: !!f.deleted,
    })),
  };
}

export function createApp(deps: Deps) {
  const { db, bucket, env, now } = deps;
  const cfg: Config = config(env);
  const fetcher = deps.fetcher ?? fetch;

  async function load(id: string) {
    return db.first<SubmissionRow>("SELECT * FROM submissions WHERE id = ?", id);
  }
  /** The submission, if the request carries its creator token. */
  async function owned(req: Request, id: string) {
    const s = await load(id);
    const token = bearer(req);
    if (!s || !token || (await sha256hex(token)) !== s.token_hash) return null;
    return s;
  }

  async function info() {
    return json({
      open: cfg.open, termsUrl: cfg.termsUrl, termsVersion: cfg.termsVersion, partSize: cfg.partSize,
      maxBuildBytes: cfg.maxBuild, maxScreenshotBytes: cfg.maxScreenshot, maxTotalBytes: cfg.maxTotal,
      categories: CATEGORIES, licenses: LICENSES, offers: OFFERS,
    });
  }

  async function create(req: Request) {
    if (!cfg.open) return fail(503, "Plugin submissions are closed right now. Please try again later.");
    let body: unknown;
    try { body = await req.json(); } catch { return fail(400, "Send the submission as JSON."); }
    let clean;
    try { clean = validateSubmission(body, cfg); } catch (e) {
      if (e instanceof Invalid) return fail(422, e.message, e.field);
      throw e;
    }
    const t = now();
    const ip = req.headers.get("cf-connecting-ip") ?? "unknown";
    const ipHash = await sha256hex(`${env.IP_SALT ?? "fueledby-submissions"}:${ip}`);
    const perIp = await db.first<{ n: number }>("SELECT COUNT(*) AS n FROM submissions WHERE ip_hash = ? AND created_at > ?", ipHash, t - DAY);
    if ((perIp?.n ?? 0) >= cfg.ratePerIp) return fail(429, "That's a lot of submissions from one place today. Please try again tomorrow.");
    const all = await db.first<{ n: number }>("SELECT COUNT(*) AS n FROM submissions WHERE created_at > ?", t - DAY);
    if ((all?.n ?? 0) >= cfg.rateGlobal) return fail(429, "We've had a lot of submissions today. Please try again tomorrow.");

    const id = "sub_" + randomString("0123456789abcdefghijklmnopqrstuvwxyz", 16);
    const token = b64url(crypto.getRandomValues(new Uint8Array(32)));
    let ref = "";
    for (let i = 0; i < 5 && !ref; i++) {
      const r = "FB-" + randomString(REF_ALPHABET, 5);
      if (!(await db.first("SELECT 1 AS x FROM submissions WHERE ref = ?", r))) ref = r;
    }
    if (!ref) return fail(500, "Couldn't make a reference code. Please try again.");
    const p = clean.plugin;
    await db.run(
      `INSERT INTO submissions (id, ref, token_hash, status, plugin_name, slug, creator_name, email, version, tagline, description,
         category, source_url, homepage, license, offer, price_usd, tested_in, notes, platforms, formats, terms_version, hub_version,
         ip_hash, created_at, updated_at)
       VALUES (?, ?, ?, 'uploading', ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
      id, ref, await sha256hex(token), p.name, slugify(p.name), p.creator, p.email, p.version, p.tagline, p.description,
      p.category, p.sourceUrl, p.homepage, p.license, p.offer, p.priceUsd, p.testedIn, p.notes,
      JSON.stringify(clean.platforms), JSON.stringify(clean.formats), clean.termsVersion, clean.hubVersion, ipHash, t, t);
    for (const f of clean.files)
      await db.run(
        `INSERT INTO submission_files (submission_id, name, kind, size, sha256, content_type, bundles, r2_key) VALUES (?, ?, ?, ?, ?, ?, ?, ?)`,
        id, f.name, f.kind, f.size, f.sha256, f.contentType, f.bundles ? JSON.stringify(f.bundles) : null, `submissions/${id}/${f.name}`);
    return json({
      id, ref, token, status: "uploading", partSize: cfg.partSize,
      files: clean.files.map((f) => ({ name: f.name, parts: partCount(f.size, cfg.partSize) })),
    }, 201);
  }

  async function putPart(req: Request, id: string, name: string, n: number) {
    const s = await owned(req, id);
    if (!s) return fail(404, "Submission not found.");
    if (s.status !== "uploading") return fail(409, "This submission is already sent.");
    const f = await db.first<FileRow>("SELECT * FROM submission_files WHERE submission_id = ? AND name = ?", id, name);
    if (!f) return fail(404, "No such file in this submission.");
    const count = partCount(f.size, cfg.partSize);
    if (!Number.isInteger(n) || n < 1 || n > count) return fail(400, `Part must be 1 to ${count}.`);
    const expected = partLength(f.size, cfg.partSize, n);
    const declared = Number(req.headers.get("content-length"));
    if (req.headers.has("content-length") && declared !== expected) return fail(400, `Part ${n} must be ${expected} bytes.`);
    if (!req.body) return fail(400, "Empty part.");

    let uploadId = f.upload_id;
    if (!uploadId) {
      const created = await bucket.createMultipartUpload(f.r2_key, { httpMetadata: { contentType: f.content_type } });
      await db.run("UPDATE submission_files SET upload_id = ? WHERE submission_id = ? AND name = ? AND upload_id IS NULL", created.uploadId, id, name);
      const now2 = await db.first<FileRow>("SELECT * FROM submission_files WHERE submission_id = ? AND name = ?", id, name);
      uploadId = now2!.upload_id!;
      if (uploadId !== created.uploadId) await bucket.resumeMultipartUpload(f.r2_key, created.uploadId).abort();
    }
    // Without a Content-Length the body is read to check its size (small parts only, in practice tests).
    const body = req.headers.has("content-length") ? req.body : new Uint8Array(await req.arrayBuffer());
    if (body instanceof Uint8Array && body.length !== expected) return fail(400, `Part ${n} must be ${expected} bytes.`);
    const part = await bucket.resumeMultipartUpload(f.r2_key, uploadId).uploadPart(n, body);
    await db.run(
      `INSERT INTO submission_parts (submission_id, name, part, etag) VALUES (?, ?, ?, ?)
       ON CONFLICT (submission_id, name, part) DO UPDATE SET etag = excluded.etag`, id, name, n, part.etag);
    return json({ part: n, of: count });
  }

  async function finalize(req: Request, id: string) {
    const s = await owned(req, id);
    if (!s) return fail(404, "Submission not found.");
    if (s.status !== "uploading") return json(publicView(s));
    const files = await db.all<FileRow>("SELECT * FROM submission_files WHERE submission_id = ? ORDER BY name", id);
    for (const f of files) {
      if (f.done) continue;
      const count = partCount(f.size, cfg.partSize);
      const parts = await db.all<{ part: number; etag: string }>(
        "SELECT part, etag FROM submission_parts WHERE submission_id = ? AND name = ? ORDER BY part", id, f.name);
      if (parts.length !== count || !f.upload_id) return fail(409, `${f.name} isn't fully uploaded yet (${parts.length} of ${count} parts).`);
      await bucket.resumeMultipartUpload(f.r2_key, f.upload_id).complete(parts.map((p) => ({ partNumber: p.part, etag: p.etag })));
      const head = await bucket.head(f.r2_key);
      if (!head || head.size !== f.size) {
        await bucket.delete(f.r2_key);
        await db.run("UPDATE submission_files SET upload_id = NULL WHERE submission_id = ? AND name = ?", id, f.name);
        await db.run("DELETE FROM submission_parts WHERE submission_id = ? AND name = ?", id, f.name);
        return fail(409, `${f.name} arrived with the wrong size. Please send it again.`);
      }
      await db.run("UPDATE submission_files SET done = 1, upload_id = NULL WHERE submission_id = ? AND name = ?", id, f.name);
      await db.run("DELETE FROM submission_parts WHERE submission_id = ? AND name = ?", id, f.name);
    }
    const sourceStatus = await checkSource(s.source_url);
    const t = now();
    await db.run("UPDATE submissions SET status = 'pending', submitted_at = ?, updated_at = ?, source_status = ? WHERE id = ? AND status = 'uploading'",
      t, t, sourceStatus, id);
    const done = (await load(id))!;
    await notify(done, files);
    return json(publicView(done));
  }

  /** HTTP status of the source link, 0 when it can't be reached. Recorded for the reviewer, never blocks. */
  async function checkSource(url: string) {
    try {
      const r = await fetcher(url, { method: "GET", redirect: "follow", headers: { "user-agent": "fueledby-submissions" }, signal: AbortSignal.timeout(5000) });
      try { await r.body?.cancel(); } catch { /* ignore */ }
      return r.status;
    } catch { return 0; }
  }

  async function notify(s: SubmissionRow, files: FileRow[]) {
    if (!env.DISCORD_WEBHOOK_URL) return;
    const builds = JSON.parse(s.formats) as Record<string, string[]>;
    const lines = [
      `**New plugin submission ${s.ref}**: ${s.plugin_name} ${s.version} by ${s.creator_name}`,
      `${s.category} · ${s.license} · offer: ${s.offer}`,
      Object.entries(builds).map(([pl, f]) => `${pl}: ${f.join(", ")}`).join(" · ") + ` · ${files.length} files`,
      `Source: <${s.source_url}> (${s.source_status ? "HTTP " + s.source_status : "unreachable"})`,
    ];
    try {
      await fetcher(env.DISCORD_WEBHOOK_URL, {
        method: "POST", headers: { "content-type": "application/json" },
        body: JSON.stringify({ content: lines.join("\n").slice(0, 1900), allowed_mentions: { parse: [] } }),
      });
    } catch { /* a missed ping is fine: the queue is the source of truth */ }
  }

  async function status(req: Request, id: string) {
    const s = await owned(req, id);
    return s ? json(publicView(s)) : fail(404, "Submission not found.");
  }

  async function withdraw(req: Request, id: string) {
    const s = await owned(req, id);
    if (!s) return fail(404, "Submission not found.");
    if (s.status === "approved") return fail(409, "This plugin is already approved. Email us to take it down.");
    if (s.status !== "withdrawn") {
      const t = now();
      await db.run("UPDATE submissions SET status = 'withdrawn', updated_at = ? WHERE id = ?", t, id);
    }
    return json(publicView((await load(id))!));
  }

  // ------------------------------------------------------------------ admin

  async function adminList(url: URL) {
    const st = url.searchParams.get("status");
    const rows = st
      ? await db.all<SubmissionRow>("SELECT * FROM submissions WHERE status = ? ORDER BY created_at DESC LIMIT 200", st)
      : await db.all<SubmissionRow>("SELECT * FROM submissions WHERE status != 'uploading' ORDER BY created_at DESC LIMIT 200");
    return json({ submissions: rows.map((s) => ({ ...publicView(s), creator: s.creator_name, email: s.email, platforms: JSON.parse(s.platforms) })) });
  }
  async function adminGet(id: string) {
    const s = await load(id);
    if (!s) return fail(404, "Submission not found.");
    const files = await db.all<FileRow>("SELECT * FROM submission_files WHERE submission_id = ? ORDER BY kind, name", id);
    return json(adminView(s, files));
  }
  async function adminFile(id: string, name: string) {
    const f = await db.first<FileRow>("SELECT * FROM submission_files WHERE submission_id = ? AND name = ?", id, name);
    if (!f || !f.done || f.deleted) return fail(404, "File not found.");
    const o = await bucket.get(f.r2_key);
    if (!o?.body) return fail(404, "File not found.");
    return new Response(o.body, {
      headers: {
        "content-type": f.content_type, "content-length": String(o.size), "x-sha256": f.sha256,
        "content-disposition": `attachment; filename="${f.name}"`, "cache-control": "no-store",
      },
    });
  }
  async function adminReview(req: Request, id: string) {
    const s = await load(id);
    if (!s) return fail(404, "Submission not found.");
    let b: { status?: string; note?: string };
    try { b = await req.json(); } catch { return fail(400, "Send JSON {status, note}."); }
    if (!b.status || !REVIEW_STATUSES.includes(b.status)) return fail(422, `status must be one of ${REVIEW_STATUSES.join(", ")}.`, "status");
    if (s.status === "uploading" || s.status === "withdrawn") return fail(409, `Can't review a submission that is ${s.status}.`);
    const note = typeof b.note === "string" ? b.note.trim().slice(0, 2000) : null;
    const t = now();
    await db.run("UPDATE submissions SET status = ?, review_note = ?, reviewed_at = ?, updated_at = ? WHERE id = ?", b.status, note || null, t, t, id);
    return adminGet(id);
  }

  return async function handle(req: Request): Promise<Response> {
    const url = new URL(req.url);
    const path = url.pathname.replace(/\/+$/, "") || "/";
    const m = req.method;
    try {
      if (path === "/" && m === "GET") return json({ service: "fueledby plugin submissions", docs: "https://github.com/askp-dev/hub/tree/main/submissions" });
      if (path === "/v1/info" && m === "GET") return info();
      if (path === "/v1/submissions" && m === "POST") return await create(req);
      let r = path.match(/^\/v1\/submissions\/(sub_[a-z0-9]{16})$/);
      if (r && m === "GET") return await status(req, r[1]);
      if (r && m === "DELETE") return await withdraw(req, r[1]);
      r = path.match(/^\/v1\/submissions\/(sub_[a-z0-9]{16})\/finalize$/);
      if (r && m === "POST") return await finalize(req, r[1]);
      r = path.match(/^\/v1\/submissions\/(sub_[a-z0-9]{16})\/files\/([^/]+)\/parts\/(\d{1,5})$/);
      if (r && m === "PUT") return await putPart(req, r[1], decodeURIComponent(r[2]), Number(r[3]));

      if (path.startsWith("/admin/")) {
        if (!env.ADMIN_TOKEN || !sameSecret(bearer(req), env.ADMIN_TOKEN)) return fail(401, "Unauthorized.");
        if (path === "/admin/submissions" && m === "GET") return await adminList(url);
        r = path.match(/^\/admin\/submissions\/(sub_[a-z0-9]{16})$/);
        if (r && m === "GET") return await adminGet(r[1]);
        r = path.match(/^\/admin\/submissions\/(sub_[a-z0-9]{16})\/review$/);
        if (r && m === "POST") return await adminReview(req, r[1]);
        r = path.match(/^\/admin\/submissions\/(sub_[a-z0-9]{16})\/files\/([^/]+)$/);
        if (r && m === "GET") return await adminFile(r[1], decodeURIComponent(r[2]));
      }
      return fail(404, "Not found.");
    } catch (e) {
      console.error(e);
      return fail(500, "Something went wrong on our side. Please try again.");
    }
  };
}

/** Hourly: drop uploads never finished after 24 h, and binaries of rejected/withdrawn submissions after 30 days. */
export async function cleanup(deps: Pick<Deps, "db" | "bucket" | "now">) {
  const { db, bucket, now } = deps;
  const t = now();
  const stale = await db.all<{ id: string }>("SELECT id FROM submissions WHERE status = 'uploading' AND created_at < ?", t - DAY);
  for (const { id } of stale) {
    const files = await db.all<FileRow>("SELECT * FROM submission_files WHERE submission_id = ?", id);
    for (const f of files) {
      if (f.upload_id) { try { await bucket.resumeMultipartUpload(f.r2_key, f.upload_id).abort(); } catch { /* already gone */ } }
      await bucket.delete(f.r2_key);
    }
    await db.run("DELETE FROM submission_parts WHERE submission_id = ?", id);
    await db.run("DELETE FROM submission_files WHERE submission_id = ?", id);
    await db.run("DELETE FROM submissions WHERE id = ?", id);
  }
  const old = await db.all<FileRow>(
    `SELECT f.* FROM submission_files f JOIN submissions s ON s.id = f.submission_id
     WHERE s.status IN ('rejected', 'withdrawn') AND s.updated_at < ? AND f.deleted = 0`, t - 30 * DAY);
  for (const f of old) {
    await bucket.delete(f.r2_key);
    await db.run("UPDATE submission_files SET deleted = 1 WHERE submission_id = ? AND name = ?", f.submission_id, f.name);
  }
  return { removedUnfinished: stale.length, removedFiles: old.length };
}

export { STATUSES };
