import type { Config } from "./env";

export const CATEGORIES = [
  "Dynamics", "EQ & filter", "Saturation & distortion", "Reverb & space", "Delay", "Modulation",
  "Pitch & vocal", "Amp & cab", "Utility & analysis", "Instrument", "Other",
] as const;
export const LICENSES = [
  "GPL-3.0", "AGPL-3.0", "LGPL-3.0", "MIT", "BSD-3-Clause", "Apache-2.0", "MPL-2.0", "ISC", "Other",
] as const;
export const OFFERS = ["free", "store", "subscription", "undecided"] as const;
export const FORMATS = ["vst3", "au", "clap"] as const;
export const BUNDLE_EXT: Record<string, string> = { vst3: ".vst3", au: ".component", clap: ".clap" };
export type FileKind = "build-mac" | "build-win" | "screenshot";

export interface FileIn {
  name: string;
  kind: FileKind;
  size: number;
  sha256: string;
  /** builds only: which bundle each format's folder holds, e.g. {"vst3": "MyVerb.vst3"} */
  bundles?: Record<string, string>;
}

export interface SubmissionIn {
  plugin: {
    name: string; creator: string; email: string; version: string; tagline: string; description: string;
    category: string; sourceUrl: string; homepage?: string; license: string; licenseOther?: string;
    offer: string; priceUsd?: number | null; testedIn?: string; notes?: string;
  };
  files: FileIn[];
  agree: { rights: boolean; source: boolean; terms: boolean; termsVersion: string };
  hubVersion?: string;
}

export class Invalid extends Error {
  constructor(public field: string, message: string) { super(message); }
}

const str = (v: unknown, field: string, label: string, min: number, max: number): string => {
  const s = typeof v === "string" ? v.trim() : "";
  if (s.length < min) throw new Invalid(field, min <= 1 ? `${label} is required.` : `${label} needs at least ${min} characters.`);
  if (s.length > max) throw new Invalid(field, `${label} can be at most ${max} characters.`);
  if (/[\u0000-\u0008\u000b\u000c\u000e-\u001f]/.test(s)) throw new Invalid(field, `${label} has control characters in it.`);
  return s;
};
const opt = (v: unknown, field: string, label: string, max: number) =>
  typeof v === "string" && v.trim() ? str(v, field, label, 1, max) : null;

export const EMAIL_RE = /^[^\s@"<>()[\]\\,;:]+@[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?(?:\.[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?)+$/;
export const VERSION_RE = /^\d{1,4}\.\d{1,4}(?:\.\d{1,4})?(?:[-+][0-9A-Za-z.-]{1,32})?$/;
const SHA_RE = /^[0-9a-f]{64}$/;
const FILE_RE = /^[A-Za-z0-9][A-Za-z0-9._ -]{0,99}$/;

/** A public https link. Rejects credentials, odd ports, localhost and bare IPs. */
export function publicUrl(v: unknown, field: string, label: string): string {
  const s = str(v, field, label, 1, 500);
  let u: URL;
  try { u = new URL(s); } catch { throw new Invalid(field, `${label} isn't a valid link.`); }
  if (u.protocol !== "https:") throw new Invalid(field, `${label} must start with https://`);
  if (u.username || u.password || u.port) throw new Invalid(field, `${label} can't have a user name, password or port.`);
  const h = u.hostname.toLowerCase();
  if (!h.includes(".") || h === "localhost" || h.endsWith(".local") || h.endsWith(".internal")
      || /^[\d.]+$/.test(h) || h.startsWith("[")) throw new Invalid(field, `${label} must be a public web address.`);
  return u.toString();
}

export function slugify(name: string): string {
  return name.toLowerCase().normalize("NFKD").replace(/[^a-z0-9]+/g, "").slice(0, 24) || "plugin";
}

export interface Clean {
  plugin: {
    name: string; creator: string; email: string; version: string; tagline: string; description: string;
    category: string; sourceUrl: string; homepage: string | null; license: string; offer: string;
    priceUsd: number | null; testedIn: string | null; notes: string | null;
  };
  files: (FileIn & { contentType: string })[];
  platforms: string[];
  formats: Record<string, string[]>;
  termsVersion: string;
  hubVersion: string | null;
}

export function validateSubmission(body: unknown, cfg: Config): Clean {
  if (!body || typeof body !== "object") throw new Invalid("body", "Send the submission as JSON.");
  const b = body as SubmissionIn;
  const p = (b.plugin ?? {}) as SubmissionIn["plugin"];

  const name = str(p.name, "name", "Plugin name", 1, 60);
  const creator = str(p.creator, "creator", "Creator name", 1, 80);
  const email = str(p.email, "email", "Email", 3, 254).toLowerCase();
  if (!EMAIL_RE.test(email)) throw new Invalid("email", "That email address doesn't look right.");
  const version = str(p.version, "version", "Version", 1, 40);
  if (!VERSION_RE.test(version)) throw new Invalid("version", "Use a version like 1.0.0.");
  const tagline = str(p.tagline, "tagline", "Short description", 1, 140);
  const description = str(p.description, "description", "Description", 20, 4000);
  const category = str(p.category, "category", "Category", 1, 40);
  if (!(CATEGORIES as readonly string[]).includes(category)) throw new Invalid("category", "Pick a category from the list.");
  const sourceUrl = publicUrl(p.sourceUrl, "sourceUrl", "Source code link");
  const homepage = typeof p.homepage === "string" && p.homepage.trim() ? publicUrl(p.homepage, "homepage", "Website") : null;
  let license = str(p.license, "license", "License", 1, 40);
  if (!(LICENSES as readonly string[]).includes(license)) throw new Invalid("license", "Pick a license from the list.");
  if (license === "Other") license = "Other: " + str(p.licenseOther, "licenseOther", "License name", 2, 80);
  const offer = str(p.offer, "offer", "How you want to offer it", 1, 20);
  if (!(OFFERS as readonly string[]).includes(offer)) throw new Invalid("offer", "Pick how you want to offer it.");
  let priceUsd: number | null = null;
  if (p.priceUsd !== undefined && p.priceUsd !== null && (p.priceUsd as unknown) !== "") {
    priceUsd = Number(p.priceUsd);
    if (!Number.isFinite(priceUsd) || priceUsd < 0 || priceUsd > 999) throw new Invalid("priceUsd", "Price must be between 0 and 999 USD.");
    priceUsd = Math.round(priceUsd * 100) / 100;
  }
  const testedIn = opt(p.testedIn, "testedIn", "Tested in", 200);
  const notes = opt(p.notes, "notes", "Notes", 2000);

  const a = b.agree ?? ({} as SubmissionIn["agree"]);
  if (a.rights !== true) throw new Invalid("agree.rights", "Confirm you made this plugin or may distribute it.");
  if (a.source !== true) throw new Invalid("agree.source", "Confirm the source link is public and matches these builds.");
  if (a.terms !== true) throw new Invalid("agree.terms", "Agree to the community creator terms to submit.");
  if (a.termsVersion !== cfg.termsVersion) throw new Invalid("agree.termsVersion", "The terms have changed. Refresh and read them again.");

  if (!Array.isArray(b.files) || b.files.length === 0) throw new Invalid("files", "Add at least one build.");
  if (b.files.length > 8) throw new Invalid("files", "Too many files.");
  const seen = new Set<string>();
  const files: Clean["files"] = [];
  const formats: Record<string, string[]> = {};
  let total = 0, shots = 0;
  for (const f of b.files) {
    if (!f || typeof f !== "object") throw new Invalid("files", "A file entry is malformed.");
    const fname = typeof f.name === "string" ? f.name : "";
    if (!FILE_RE.test(fname) || fname.includes("..")) throw new Invalid("files", `File name "${fname}" isn't allowed.`);
    if (seen.has(fname.toLowerCase())) throw new Invalid("files", `Two files are called ${fname}.`);
    seen.add(fname.toLowerCase());
    if (!Number.isSafeInteger(f.size) || f.size <= 0) throw new Invalid("files", `${fname} is empty.`);
    if (typeof f.sha256 !== "string" || !SHA_RE.test(f.sha256.toLowerCase())) throw new Invalid("files", `${fname} needs a SHA-256 checksum.`);
    total += f.size;
    if (f.kind === "screenshot") {
      if (++shots > 4) throw new Invalid("files", "Up to 4 screenshots.");
      const ext = fname.toLowerCase().split(".").pop();
      if (ext !== "png" && ext !== "jpg" && ext !== "jpeg") throw new Invalid("files", "Screenshots must be PNG or JPEG.");
      if (f.size > cfg.maxScreenshot) throw new Invalid("files", `${fname} is over ${cfg.maxScreenshot / 1048576} MB.`);
      files.push({ name: fname, kind: f.kind, size: f.size, sha256: f.sha256.toLowerCase(), contentType: ext === "png" ? "image/png" : "image/jpeg" });
    } else if (f.kind === "build-mac" || f.kind === "build-win") {
      const platform = f.kind === "build-mac" ? "mac" : "win";
      if (formats[platform]) throw new Invalid("files", `Only one ${platform === "mac" ? "Mac" : "Windows"} build per submission.`);
      if (!fname.toLowerCase().endsWith(".zip")) throw new Invalid("files", "Builds are uploaded as .zip files.");
      if (f.size > cfg.maxBuild) throw new Invalid("files", `${fname} is over ${cfg.maxBuild / 1048576} MB.`);
      const bundles = f.bundles && typeof f.bundles === "object" ? f.bundles : {};
      const fmts: string[] = [];
      for (const [fmt, bundle] of Object.entries(bundles)) {
        if (!(FORMATS as readonly string[]).includes(fmt)) throw new Invalid("files", `Unknown format ${fmt}.`);
        if (fmt === "au" && platform !== "mac") throw new Invalid("files", "AU is Mac only.");
        if (typeof bundle !== "string" || !FILE_RE.test(bundle) || !bundle.toLowerCase().endsWith(BUNDLE_EXT[fmt]))
          throw new Invalid("files", `The ${fmt.toUpperCase()} bundle name doesn't look right.`);
        fmts.push(fmt);
      }
      if (fmts.length === 0) throw new Invalid("files", `The ${platform === "mac" ? "Mac" : "Windows"} build has no VST3, AU or CLAP in it.`);
      formats[platform] = FORMATS.filter((x) => fmts.includes(x));
      files.push({ name: fname, kind: f.kind, size: f.size, sha256: f.sha256.toLowerCase(), bundles, contentType: "application/zip" });
    } else {
      throw new Invalid("files", "Unknown file kind.");
    }
  }
  if (Object.keys(formats).length === 0) throw new Invalid("files", "Add a Mac or Windows build.");
  if (total > cfg.maxTotal) throw new Invalid("files", `All files together are over ${cfg.maxTotal / 1048576} MB.`);

  const hubVersion = typeof b.hubVersion === "string" && /^[0-9A-Za-z.+-]{1,20}$/.test(b.hubVersion) ? b.hubVersion : null;
  return {
    plugin: { name, creator, email, version, tagline, description, category, sourceUrl, homepage, license, offer, priceUsd, testedIn, notes },
    files,
    platforms: ["mac", "win"].filter((x) => formats[x]),
    formats,
    termsVersion: a.termsVersion,
    hubVersion,
  };
}
