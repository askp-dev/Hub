-- Plugin submissions from the fueledby Hub. One row per submission, one per file.
CREATE TABLE submissions (
  id             TEXT PRIMARY KEY,           -- sub_xxxxxxxxxxxx
  ref            TEXT NOT NULL UNIQUE,       -- short code shown to the creator, e.g. FB-7K3Q2
  token_hash     TEXT NOT NULL,              -- sha256 of the creator's status token
  status         TEXT NOT NULL,              -- uploading | pending | in_review | changes_requested | approved | rejected | withdrawn
  plugin_name    TEXT NOT NULL,
  slug           TEXT NOT NULL,              -- suggested Hub id
  creator_name   TEXT NOT NULL,
  email          TEXT NOT NULL,
  version        TEXT NOT NULL,
  tagline        TEXT NOT NULL,
  description    TEXT NOT NULL,
  category       TEXT NOT NULL,
  source_url     TEXT NOT NULL,
  homepage       TEXT,
  license        TEXT NOT NULL,
  offer          TEXT NOT NULL,              -- free | store | subscription | undecided
  price_usd      REAL,
  tested_in      TEXT,
  notes          TEXT,
  platforms      TEXT NOT NULL,              -- JSON ["mac","win"]
  formats        TEXT NOT NULL,              -- JSON {"mac":["vst3","au"],"win":["vst3"]}
  terms_version  TEXT NOT NULL,
  hub_version    TEXT,
  ip_hash        TEXT NOT NULL,
  source_status  INTEGER,                    -- HTTP status of the source link at submit time (0 = unreachable)
  review_note    TEXT,                       -- shown to the creator in the Hub
  created_at     INTEGER NOT NULL,
  submitted_at   INTEGER,
  updated_at     INTEGER NOT NULL,
  reviewed_at    INTEGER
);
CREATE INDEX submissions_status ON submissions (status, created_at);
CREATE INDEX submissions_ip ON submissions (ip_hash, created_at);

CREATE TABLE submission_files (
  submission_id  TEXT NOT NULL REFERENCES submissions (id),
  name           TEXT NOT NULL,              -- file name as uploaded, e.g. MyVerb-mac.zip
  kind           TEXT NOT NULL,              -- build-mac | build-win | screenshot
  size           INTEGER NOT NULL,
  sha256         TEXT NOT NULL,              -- from the Hub; the review script checks it after download
  content_type   TEXT NOT NULL,
  bundles        TEXT,                       -- builds: JSON {"vst3":"MyVerb.vst3",...}
  r2_key         TEXT NOT NULL,
  upload_id      TEXT,                       -- R2 multipart upload, until complete
  done           INTEGER NOT NULL DEFAULT 0,
  deleted        INTEGER NOT NULL DEFAULT 0, -- binary removed from R2 by cleanup
  PRIMARY KEY (submission_id, name)
);

CREATE TABLE submission_parts (
  submission_id  TEXT NOT NULL,
  name           TEXT NOT NULL,
  part           INTEGER NOT NULL,
  etag           TEXT NOT NULL,
  PRIMARY KEY (submission_id, name, part)
);
