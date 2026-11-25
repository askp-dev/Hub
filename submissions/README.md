# fueledby plugin submissions

The service behind the Hub's **Submit a plugin** page. Community creators send their builds, a public
source link and their details; everything lands in a review queue. **Nothing is published
automatically**: an approved plugin still goes out through the normal Hub flow
(`tools/package-plugin.sh`, a release, `manifest.json`).

Cloudflare Worker + D1 (details) + R2 (files). Runs on the free plan: uploads go through the Worker in
8 MB parts (R2 multipart), so no request is near the 100 MB body limit, and the Worker never reads the
bytes (no CPU cost). The Hub sends a SHA-256 for every file; `scripts/review.mjs download` checks them.

## What a submission holds

| Required | Optional |
| --- | --- |
| Plugin name, version, short + long description, category | Website |
| Creator name, **email** (never shown publicly) | Screenshots (up to 4, PNG/JPEG) |
| **Source code link** (public https; the Worker records its HTTP status for the reviewer) | Tested in (DAWs) |
| License (GPL-3.0, MIT, ... or Other + name) | Price idea (Store only) |
| **A Mac and/or Windows build** (VST3 / AU / CLAP, zipped by the Hub in the VST3/ AU/ CLAP/ layout) | Notes for the reviewer |
| How to offer it: free, fueledby Store, fueledby subscription, not sure | |
| Three agreements: rights, source matches builds, community creator terms (version recorded) | |

Statuses: `uploading` -> `pending` -> `in_review` -> `approved` / `changes_requested` / `rejected`; the
creator can withdraw. The Hub shows each of the creator's submissions with its status and review note.

## API

| | |
| --- | --- |
| `GET /v1/info` | open?, terms URL + version, limits, categories, licenses |
| `POST /v1/submissions` | details + file list (name, kind, size, sha256, bundles) -> `{id, ref, token, partSize}` |
| `PUT /v1/submissions/:id/files/:name/parts/:n` | one part (Bearer token) |
| `POST /v1/submissions/:id/finalize` | completes the uploads, checks sizes, -> `pending` |
| `GET` / `DELETE /v1/submissions/:id` | status + review note / withdraw (Bearer token) |
| `/admin/...` | list, read, download, review (Bearer `ADMIN_TOKEN`) |

Limits (wrangler.toml): 300 MB per build, 8 MB per screenshot, 700 MB total, 5 submissions per IP per day,
100 per day overall. `SUBMISSIONS_OPEN = "off"` closes the page. Hourly cron: unfinished uploads are
removed after 24 h, binaries of rejected/withdrawn submissions after 30 days (details stay).

## Review

```bash
export SUBMIT_URL=https://submit.fueledby.cc ADMIN_TOKEN=...
npm run review -- list                     # pending
npm run review -- show FB-7K3Q2
npm run review -- download FB-7K3Q2        # into review/FB-7K3Q2, checks every SHA-256
npm run review -- set FB-7K3Q2 changes_requested "Please add a CLAP build for Windows."
```

Set `DISCORD_WEBHOOK_URL` to get a message per new submission.

## Develop and test

```bash
npm ci
npm test                 # vitest: flow, part sizes, rate limits, cleanup, validation
npm run typecheck
npm run db:migrate:local && npm run dev    # local Worker with local D1 + R2 on :8787
# then, from a Hub build:
"fueledby Hub" --submit-url http://127.0.0.1:8787 --submit-json spec.json
```

## Deploy (first time)

```bash
npx wrangler d1 create fueledby-submissions      # put the id in wrangler.toml
npx wrangler r2 bucket create fueledby-submissions
npm run db:migrate:remote
npx wrangler secret put ADMIN_TOKEN
npx wrangler secret put IP_SALT
# (submit.fueledby.cc route is already in wrangler.toml)
npx wrangler deploy
```
