import { describe, expect, it } from "vitest";
import { bytes, harness, sha, validBody } from "./helpers";
import { partLength } from "../src/app";

const MB = 1024 * 1024;

async function upload(h: ReturnType<typeof harness>, id: string, token: string, name: string, data: Uint8Array, partSize: number) {
  const count = Math.max(1, Math.ceil(data.length / partSize));
  for (let n = 1; n <= count; n++) {
    const r = await h.call("PUT", `/v1/submissions/${id}/files/${encodeURIComponent(name)}/parts/${n}`,
      { token, raw: data.subarray((n - 1) * partSize, n * partSize) });
    expect(r.status, await r.clone().text()).toBe(200);
  }
}

describe("submission flow", () => {
  it("creates, uploads in parts, finalizes, and lands in the review queue", async () => {
    const h = harness({ DISCORD_WEBHOOK_URL: "https://discord.test/hook" });
    const mac = bytes(12 * MB + 123, 3), win = bytes(2 * MB, 5), shot = bytes(40_000, 7);
    const body = validBody([
      { name: "MossVerb-mac.zip", kind: "build-mac", data: mac, bundles: { vst3: "MossVerb.vst3", au: "MossVerb.component", clap: "MossVerb.clap" } },
      { name: "MossVerb-win.zip", kind: "build-win", data: win, bundles: { vst3: "MossVerb.vst3" } },
      { name: "screen-1.png", kind: "screenshot", data: shot },
    ]);
    const r = await h.call("POST", "/v1/submissions", { body });
    expect(r.status).toBe(201);
    const c = await r.json() as { id: string; ref: string; token: string; partSize: number; files: { name: string; parts: number }[] };
    expect(c.ref).toMatch(/^FB-[0-9A-Z]{5}$/);
    expect(c.partSize).toBe(5 * MB);
    expect(c.files.find((f) => f.name === "MossVerb-mac.zip")!.parts).toBe(3);

    // finalize before the uploads are done is refused
    expect((await h.call("POST", `/v1/submissions/${c.id}/finalize`, { token: c.token })).status).toBe(409);

    await upload(h, c.id, c.token, "MossVerb-mac.zip", mac, c.partSize);
    await upload(h, c.id, c.token, "MossVerb-win.zip", win, c.partSize);
    await upload(h, c.id, c.token, "screen-1.png", shot, c.partSize);
    // a retried part is fine
    await upload(h, c.id, c.token, "screen-1.png", shot, c.partSize);

    const fin = await h.call("POST", `/v1/submissions/${c.id}/finalize`, { token: c.token });
    expect(fin.status).toBe(200);
    expect((await fin.json() as { status: string }).status).toBe("pending");
    expect(h.calls.some((x) => x.url === "https://github.com/example/moss-verb")).toBe(true);
    expect(h.calls.find((x) => x.url.includes("discord"))!.body).toContain("Moss Verb 1.0.0 by Rae Okafor");

    // the creator sees the status with their token only
    expect((await h.call("GET", `/v1/submissions/${c.id}`)).status).toBe(404);
    expect((await h.call("GET", `/v1/submissions/${c.id}`, { token: "wrong" })).status).toBe(404);
    const st = await (await h.call("GET", `/v1/submissions/${c.id}`, { token: c.token })).json() as Record<string, unknown>;
    expect(st.status).toBe("pending");
    expect(st.email).toBeUndefined();   // the public view never echoes personal data

    // admin: list, read, download, review
    expect((await h.call("GET", "/admin/submissions")).status).toBe(401);
    const list = await (await h.call("GET", "/admin/submissions", { token: "admin-secret" })).json() as { submissions: { id: string }[] };
    expect(list.submissions.map((s) => s.id)).toEqual([c.id]);
    const full = await (await h.call("GET", `/admin/submissions/${c.id}`, { token: "admin-secret" })).json() as Record<string, any>;
    expect(full.email).toBe("rae@example.com");
    expect(full.slug).toBe("mossverb");
    expect(full.sourceStatus).toBe(200);
    expect(full.formats).toEqual({ mac: ["vst3", "au", "clap"], win: ["vst3"] });
    const dl = await h.call("GET", `/admin/submissions/${c.id}/files/MossVerb-mac.zip`, { token: "admin-secret" });
    expect(dl.status).toBe(200);
    expect(sha(new Uint8Array(await dl.arrayBuffer()))).toBe(sha(mac));
    expect(dl.headers.get("x-sha256")).toBe(sha(mac));
    const rev = await h.call("POST", `/admin/submissions/${c.id}/review`, { token: "admin-secret", body: { status: "changes_requested", note: "Please add a CLAP build for Windows." } });
    expect(rev.status).toBe(200);
    const after = await (await h.call("GET", `/v1/submissions/${c.id}`, { token: c.token })).json() as Record<string, unknown>;
    expect(after.status).toBe("changes_requested");
    expect(after.reviewNote).toBe("Please add a CLAP build for Windows.");
  });

  it("checks part sizes", async () => {
    const h = harness();
    const data = bytes(6 * MB);
    const c = await (await h.call("POST", "/v1/submissions", { body: validBody([{ name: "a.zip", kind: "build-win", data, bundles: { clap: "A.clap" } }]) })).json() as any;
    expect(partLength(data.length, 5 * MB, 2)).toBe(MB);
    const bad = await h.call("PUT", `/v1/submissions/${c.id}/files/a.zip/parts/2`, { token: c.token, raw: bytes(10) });
    expect(bad.status).toBe(400);
    expect((await h.call("PUT", `/v1/submissions/${c.id}/files/a.zip/parts/3`, { token: c.token, raw: bytes(10) })).status).toBe(400);
    expect((await h.call("PUT", `/v1/submissions/${c.id}/files/b.zip/parts/1`, { token: c.token, raw: bytes(10) })).status).toBe(404);
    expect((await h.call("PUT", `/v1/submissions/${c.id}/files/a.zip/parts/1`, { token: "nope", raw: bytes(5 * MB) })).status).toBe(404);
  });

  it("lets the creator withdraw, and refuses uploads after sending", async () => {
    const h = harness();
    const data = bytes(1000);
    const c = await (await h.call("POST", "/v1/submissions", { body: validBody([{ name: "a.zip", kind: "build-mac", data, bundles: { au: "A.component" } }]) })).json() as any;
    await upload(h, c.id, c.token, "a.zip", data, c.partSize);
    await h.call("POST", `/v1/submissions/${c.id}/finalize`, { token: c.token });
    expect((await h.call("PUT", `/v1/submissions/${c.id}/files/a.zip/parts/1`, { token: c.token, raw: data })).status).toBe(409);
    const w = await (await h.call("DELETE", `/v1/submissions/${c.id}`, { token: c.token })).json() as any;
    expect(w.status).toBe("withdrawn");
    expect((await h.call("POST", `/admin/submissions/${c.id}/review`, { token: "admin-secret", body: { status: "approved" } })).status).toBe(409);
  });

  it("rate limits per IP and closes when SUBMISSIONS_OPEN is off", async () => {
    const h = harness({ RATE_PER_IP_PER_DAY: "2" });
    const body = validBody([{ name: "a.zip", kind: "build-win", data: bytes(10), bundles: { vst3: "A.vst3" } }]);
    expect((await h.call("POST", "/v1/submissions", { body })).status).toBe(201);
    expect((await h.call("POST", "/v1/submissions", { body })).status).toBe(201);
    expect((await h.call("POST", "/v1/submissions", { body })).status).toBe(429);
    expect((await h.call("POST", "/v1/submissions", { body, ip: "198.51.100.1" })).status).toBe(201);
    h.advance(25 * 3600_000);
    expect((await h.call("POST", "/v1/submissions", { body })).status).toBe(201);

    const closed = harness({ SUBMISSIONS_OPEN: "off" });
    expect((await closed.call("POST", "/v1/submissions", { body })).status).toBe(503);
    expect((await (await closed.call("GET", "/v1/info")).json() as any).open).toBe(false);
  });

  it("cleans up unfinished uploads after a day", async () => {
    const h = harness();
    const { cleanup } = await import("../src/app");
    const data = bytes(6 * 1024 * 1024);
    const c = await (await h.call("POST", "/v1/submissions", { body: validBody([{ name: "a.zip", kind: "build-win", data, bundles: { vst3: "A.vst3" } }]) })).json() as any;
    await h.call("PUT", `/v1/submissions/${c.id}/files/a.zip/parts/1`, { token: c.token, raw: data.subarray(0, 5 * MB) });
    const bucket = h.deps.bucket as ReturnType<typeof import("../src/storage").memoryBucket>;
    expect(bucket.uploads.size).toBe(1);
    expect((await cleanup(h.deps)).removedUnfinished).toBe(0);
    h.advance(25 * 3600_000);
    expect((await cleanup(h.deps)).removedUnfinished).toBe(1);
    expect(bucket.uploads.size).toBe(0);
    expect((await h.call("GET", `/v1/submissions/${c.id}`, { token: c.token })).status).toBe(404);
  });
});
