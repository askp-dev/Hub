import { readFileSync, readdirSync } from "node:fs";
import { DatabaseSync } from "node:sqlite";
import { createHash } from "node:crypto";
import { cleanParams, type Db } from "../src/db";
import { createApp, type Deps } from "../src/app";
import { memoryBucket } from "../src/storage";

/** In-memory SQLite with the real migrations, behind the same Db interface as D1. */
export function testDb(): Db {
  const sqlite = new DatabaseSync(":memory:");
  const dir = new URL("../migrations/", import.meta.url);
  for (const f of readdirSync(dir).filter((f) => f.endsWith(".sql")).sort()) sqlite.exec(readFileSync(new URL(f, dir), "utf8"));
  const p = (params: unknown[]) => cleanParams(params) as (string | number | null)[];
  return {
    async all<T>(sql: string, ...params: unknown[]) { return sqlite.prepare(sql).all(...p(params)) as T[]; },
    async first<T>(sql: string, ...params: unknown[]) { return (sqlite.prepare(sql).get(...p(params)) as T | undefined) ?? null; },
    async run(sql: string, ...params: unknown[]) { return { changes: Number(sqlite.prepare(sql).run(...p(params)).changes) }; },
  };
}

export const sha = (b: Uint8Array) => createHash("sha256").update(b).digest("hex");
export const bytes = (n: number, seed = 1) => { const b = new Uint8Array(n); for (let i = 0; i < n; i++) b[i] = (i * 31 + seed) & 255; return b; };

export function harness(env: Record<string, string> = {}) {
  let clock = Date.UTC(2026, 10, 25, 12);
  const calls: { url: string; body?: string }[] = [];
  const fetcher = (async (url: string | URL | Request, init?: RequestInit) => {
    calls.push({ url: String(url), body: typeof init?.body === "string" ? init.body : undefined });
    return new Response("ok", { status: String(url).includes("missing") ? 404 : 200 });
  }) as typeof fetch;
  const deps: Deps = {
    db: testDb(), bucket: memoryBucket(5 * 1024 * 1024),
    env: { ADMIN_TOKEN: "admin-secret", PART_SIZE_MB: "5", ...env }, now: () => clock, fetcher,
  };
  const app = createApp(deps);
  const call = (method: string, path: string, opts: { body?: unknown; token?: string; ip?: string; raw?: Uint8Array } = {}) => {
    const headers: Record<string, string> = { "cf-connecting-ip": opts.ip ?? "203.0.113.7" };
    if (opts.token) headers.authorization = `Bearer ${opts.token}`;
    let body: BodyInit | undefined;
    if (opts.raw) { body = opts.raw; headers["content-length"] = String(opts.raw.length); }
    else if (opts.body !== undefined) { body = JSON.stringify(opts.body); headers["content-type"] = "application/json"; }
    return app(new Request(`https://submit.test${path}`, { method, headers, body }));
  };
  return { deps, app, call, calls, advance: (ms: number) => { clock += ms; } };
}

export function validBody(files: { name: string; kind: string; data: Uint8Array; bundles?: Record<string, string> }[]) {
  return {
    plugin: {
      name: "Moss Verb", creator: "Rae Okafor", email: "Rae@Example.com", version: "1.0.0",
      tagline: "A small, warm plate reverb", description: "A plate reverb with a damping knob and a freeze switch.",
      category: "Reverb & space", sourceUrl: "https://github.com/example/moss-verb", license: "GPL-3.0",
      offer: "store", priceUsd: 19, testedIn: "Reaper, Ableton Live 12",
    },
    files: files.map((f) => ({ name: f.name, kind: f.kind, size: f.data.length, sha256: sha(f.data), bundles: f.bundles })),
    agree: { rights: true, source: true, terms: true, termsVersion: "2026-11-25" },
    hubVersion: "0.2.2",
  };
}
