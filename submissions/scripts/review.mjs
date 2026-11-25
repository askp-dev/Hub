#!/usr/bin/env node
// Review queue for plugin submissions. Nothing here publishes: approving only records the decision.
// Publishing an approved plugin is still the normal Hub flow (tools/package-plugin.sh + release + manifest).
//
//   SUBMIT_URL=https://submit.fueledby.cc ADMIN_TOKEN=... node scripts/review.mjs <command>
//
//   list [status]                       pending by default; "all" for everything sent
//   show <id|ref>                       every field, files and checksums
//   download <id|ref> [dir]             fetch every file and check its SHA-256 (default dir: ./review/<ref>)
//   set <id|ref> <status> [note...]     in_review | changes_requested | approved | rejected; the note shows in the creator's Hub
import { createHash } from "node:crypto";
import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const base = (process.env.SUBMIT_URL ?? "https://submit.fueledby.cc").replace(/\/+$/, "");
const token = process.env.ADMIN_TOKEN;
if (!token) { console.error("Set ADMIN_TOKEN (the Worker secret)."); process.exit(2); }
const auth = { authorization: `Bearer ${token}` };

async function api(path, init = {}) {
  const r = await fetch(base + path, { ...init, headers: { ...auth, ...(init.headers ?? {}) } });
  if (!r.ok) throw new Error(`${init.method ?? "GET"} ${path}: HTTP ${r.status} ${await r.text()}`);
  return r;
}
async function resolve(idOrRef) {
  if (idOrRef?.startsWith("sub_")) return idOrRef;
  const { submissions } = await (await api("/admin/submissions")).json();
  const hit = submissions.find((s) => s.ref.toUpperCase() === String(idOrRef).toUpperCase());
  if (!hit) throw new Error(`No submission ${idOrRef}`);
  return hit.id;
}
const when = (ms) => (ms ? new Date(ms).toISOString().replace("T", " ").slice(0, 16) : "-");
const mb = (n) => (n / 1048576).toFixed(1) + " MB";

const [cmd, ...args] = process.argv.slice(2);
try {
  if (cmd === "list" || !cmd) {
    const st = args[0] ?? "pending";
    const q = st === "all" ? "" : `?status=${encodeURIComponent(st)}`;
    const { submissions } = await (await api("/admin/submissions" + q)).json();
    if (!submissions.length) console.log(`No ${st} submissions.`);
    for (const s of submissions)
      console.log(`${s.ref}  ${s.id}  ${s.status.padEnd(17)} ${when(s.submittedAt)}  ${s.pluginName} ${s.version} by ${s.creator} <${s.email}> [${s.platforms.join("+")}]`);
  } else if (cmd === "show") {
    const s = await (await api(`/admin/submissions/${await resolve(args[0])}`)).json();
    const { files, description, ...rest } = s;
    for (const [k, v] of Object.entries(rest)) if (v !== null && v !== undefined) console.log(`${k.padEnd(14)} ${typeof v === "object" ? JSON.stringify(v) : k.endsWith("At") ? when(v) : v}`);
    console.log(`\n${description}\n`);
    for (const f of files) console.log(`  ${f.kind.padEnd(11)} ${f.name.padEnd(32)} ${mb(f.size).padStart(9)}  ${f.sha256}${f.bundles ? "  " + JSON.stringify(f.bundles) : ""}${f.deleted ? "  (deleted)" : ""}`);
  } else if (cmd === "download") {
    const id = await resolve(args[0]);
    const s = await (await api(`/admin/submissions/${id}`)).json();
    const dir = args[1] ?? join("review", s.ref);
    mkdirSync(dir, { recursive: true });
    let bad = 0;
    for (const f of s.files.filter((f) => f.uploaded && !f.deleted)) {
      const buf = Buffer.from(await (await api(`/admin/submissions/${id}/files/${encodeURIComponent(f.name)}`)).arrayBuffer());
      const sum = createHash("sha256").update(buf).digest("hex");
      writeFileSync(join(dir, f.name), buf);
      const ok = sum === f.sha256 && buf.length === f.size;
      if (!ok) bad++;
      console.log(`${ok ? "ok  " : "BAD "} ${f.name} ${mb(buf.length)} ${sum}`);
    }
    writeFileSync(join(dir, "submission.json"), JSON.stringify(s, null, 2));
    console.log(`\nSaved to ${dir}. ${bad ? bad + " file(s) did NOT match their checksum." : "Every checksum matches."}`);
    console.log("Next: build from the source link yourself or test these binaries, then `set` a status.");
    console.log(`To publish once approved: tools/package-plugin.sh ${s.slug} ${s.version} <artefacts dir> [win]`);
    if (bad) process.exit(1);
  } else if (cmd === "set") {
    const [ref, status, ...note] = args;
    const s = await (await api(`/admin/submissions/${await resolve(ref)}/review`, {
      method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ status, note: note.join(" ") }),
    })).json();
    console.log(`${s.ref} is now ${s.status}.${s.reviewNote ? " Note: " + s.reviewNote : ""}`);
    console.log(`Tell the creator at ${s.email} (the Hub shows the status and note too).`);
  } else {
    console.error("Commands: list [status] | show <id|ref> | download <id|ref> [dir] | set <id|ref> <status> [note]");
    process.exit(2);
  }
} catch (e) {
  console.error(e.message);
  process.exit(1);
}
