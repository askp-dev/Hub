import { describe, expect, it } from "vitest";
import { config } from "../src/env";
import { Invalid, publicUrl, slugify, validateSubmission } from "../src/validate";
import { bytes, validBody } from "./helpers";

const cfg = config({});
const build = [{ name: "a.zip", kind: "build-mac", data: bytes(100), bundles: { vst3: "A.vst3", au: "A.component" } }];
const field = (fn: () => unknown) => { try { fn(); return null; } catch (e) { return e instanceof Invalid ? e.field : String(e); } };
const withPlugin = (patch: Record<string, unknown>) => { const b = validBody(build) as any; Object.assign(b.plugin, patch); return b; };

describe("validation", () => {
  it("accepts a good submission and normalises it", () => {
    const c = validateSubmission(validBody(build), cfg);
    expect(c.plugin.email).toBe("rae@example.com");
    expect(c.platforms).toEqual(["mac"]);
    expect(c.formats).toEqual({ mac: ["vst3", "au"] });
  });
  it("requires the fields the user asked for", () => {
    expect(field(() => validateSubmission(withPlugin({ email: "" }), cfg))).toBe("email");
    expect(field(() => validateSubmission(withPlugin({ email: "not-an-email" }), cfg))).toBe("email");
    expect(field(() => validateSubmission(withPlugin({ sourceUrl: "" }), cfg))).toBe("sourceUrl");
    expect(field(() => validateSubmission(withPlugin({ sourceUrl: "http://github.com/x/y" }), cfg))).toBe("sourceUrl");
    expect(field(() => validateSubmission({ ...validBody(build), files: [] }, cfg))).toBe("files");
    const onlyShot = validBody([{ name: "s.png", kind: "screenshot", data: bytes(10) }]);
    expect(field(() => validateSubmission(onlyShot, cfg))).toBe("files");
  });
  it("checks versions, lists and agreements", () => {
    expect(field(() => validateSubmission(withPlugin({ version: "v1" }), cfg))).toBe("version");
    expect(field(() => validateSubmission(withPlugin({ category: "Stuff" }), cfg))).toBe("category");
    expect(field(() => validateSubmission(withPlugin({ license: "Other" }), cfg))).toBe("licenseOther");
    expect(validateSubmission(withPlugin({ license: "Other", licenseOther: "Zlib" }), cfg).plugin.license).toBe("Other: Zlib");
    expect(field(() => validateSubmission(withPlugin({ priceUsd: -1 }), cfg))).toBe("priceUsd");
    expect(field(() => validateSubmission({ ...validBody(build), agree: { rights: true, source: true, terms: false, termsVersion: "2026-11-25" } }, cfg))).toBe("agree.terms");
    expect(field(() => validateSubmission({ ...validBody(build), agree: { rights: true, source: true, terms: true, termsVersion: "old" } }, cfg))).toBe("agree.termsVersion");
  });
  it("checks files", () => {
    const f = (files: any[]) => field(() => validateSubmission(validBody(files), cfg));
    expect(f([{ name: "../a.zip", kind: "build-mac", data: bytes(1), bundles: { vst3: "A.vst3" } }])).toBe("files");
    expect(f([{ name: "a.zip", kind: "build-win", data: bytes(1), bundles: { au: "A.component" } }])).toBe("files");
    expect(f([{ name: "a.zip", kind: "build-win", data: bytes(1), bundles: { vst3: "A.clap" } }])).toBe("files");
    expect(f([{ name: "a.zip", kind: "build-win", data: bytes(1), bundles: {} }])).toBe("files");
    expect(f([{ name: "a.exe", kind: "build-win", data: bytes(1), bundles: { vst3: "A.vst3" } }])).toBe("files");
    expect(f([...build, { name: "s.gif", kind: "screenshot", data: bytes(1) }])).toBe("files");
    expect(f([...build, { name: "b.zip", kind: "build-mac", data: bytes(1), bundles: { vst3: "A.vst3" } }])).toBe("files");
  });
  it("keeps links public", () => {
    for (const bad of ["https://localhost/x", "https://127.0.0.1/x", "https://user:pw@github.com/x", "https://github.com:8443/x", "ftp://a.b/c"])
      expect(() => publicUrl(bad, "u", "Link")).toThrow(Invalid);
    expect(publicUrl("https://codeberg.org/me/plug", "u", "Link")).toBe("https://codeberg.org/me/plug");
  });
  it("slugs names", () => {
    expect(slugify("Moss Verb!")).toBe("mossverb");
    expect(slugify("Ünïcode Dëlay")).toBe("unicodedelay");
    expect(slugify("***")).toBe("plugin");
  });
});
