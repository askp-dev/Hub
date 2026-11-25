export interface Env {
  DB: D1Database;
  FILES: R2Bucket;
  SUBMISSIONS_OPEN?: string;
  TERMS_URL?: string;
  TERMS_VERSION?: string;
  PART_SIZE_MB?: string;
  MAX_BUILD_MB?: string;
  MAX_SCREENSHOT_MB?: string;
  MAX_TOTAL_MB?: string;
  RATE_PER_IP_PER_DAY?: string;
  RATE_GLOBAL_PER_DAY?: string;
  /** Bearer token for /admin/* (secret). Without it the review API is off. */
  ADMIN_TOKEN?: string;
  /** Salt for hashing client IPs (secret). */
  IP_SALT?: string;
  /** Optional Discord webhook: one message per new submission (secret). */
  DISCORD_WEBHOOK_URL?: string;
}

export interface Config {
  open: boolean;
  termsUrl: string;
  termsVersion: string;
  partSize: number;
  maxBuild: number;
  maxScreenshot: number;
  maxTotal: number;
  ratePerIp: number;
  rateGlobal: number;
}

const mb = (v: string | undefined, d: number) => Math.round((Number(v) > 0 ? Number(v) : d) * 1024 * 1024);
const n = (v: string | undefined, d: number) => (Number(v) > 0 ? Math.floor(Number(v)) : d);

export function config(env: Partial<Env>): Config {
  return {
    open: (env.SUBMISSIONS_OPEN ?? "on") === "on",
    termsUrl: env.TERMS_URL ?? "https://fueledby.cc/terms",
    termsVersion: env.TERMS_VERSION ?? "2026-11-25",
    // R2 multipart parts must be at least 5 MiB (except the last one).
    partSize: Math.max(mb(env.PART_SIZE_MB, 8), 5 * 1024 * 1024),
    maxBuild: mb(env.MAX_BUILD_MB, 300),
    maxScreenshot: mb(env.MAX_SCREENSHOT_MB, 8),
    maxTotal: mb(env.MAX_TOTAL_MB, 700),
    ratePerIp: n(env.RATE_PER_IP_PER_DAY, 5),
    rateGlobal: n(env.RATE_GLOBAL_PER_DAY, 100),
  };
}
