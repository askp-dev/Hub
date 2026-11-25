/** The R2 calls the app uses. Production passes the R2 binding; tests pass memoryBucket(). */
export interface UploadedPart { partNumber: number; etag: string }
export interface MultipartUpload {
  uploadPart(partNumber: number, body: ReadableStream | ArrayBuffer | Uint8Array): Promise<UploadedPart>;
  complete(parts: UploadedPart[]): Promise<unknown>;
  abort(): Promise<void>;
}
export interface StoredObject { size: number; body?: ReadableStream | null; httpMetadata?: { contentType?: string } }
export interface Bucket {
  createMultipartUpload(key: string, options?: { httpMetadata?: { contentType?: string } }): Promise<{ uploadId: string }>;
  resumeMultipartUpload(key: string, uploadId: string): MultipartUpload;
  head(key: string): Promise<StoredObject | null>;
  get(key: string): Promise<StoredObject | null>;
  delete(keys: string | string[]): Promise<void>;
}

/** In-memory bucket with R2's multipart rules (parts >= 5 MiB except the last), for tests. */
export function memoryBucket(minPart = 5 * 1024 * 1024) {
  const objects = new Map<string, { data: Uint8Array; contentType?: string }>();
  const uploads = new Map<string, { key: string; contentType?: string; parts: Map<number, Uint8Array> }>();
  let next = 1;
  const bytes = async (b: ReadableStream | ArrayBuffer | Uint8Array) =>
    b instanceof Uint8Array ? b : b instanceof ArrayBuffer ? new Uint8Array(b) : new Uint8Array(await new Response(b).arrayBuffer());
  const bucket: Bucket & { objects: typeof objects; uploads: typeof uploads } = {
    objects,
    uploads,
    async createMultipartUpload(key, options) {
      const uploadId = `up${next++}`;
      uploads.set(uploadId, { key, contentType: options?.httpMetadata?.contentType, parts: new Map() });
      return { uploadId };
    },
    resumeMultipartUpload(key, uploadId) {
      return {
        async uploadPart(n, body) {
          const u = uploads.get(uploadId);
          if (!u || u.key !== key) throw new Error("no such upload");
          u.parts.set(n, await bytes(body));
          return { partNumber: n, etag: `etag-${uploadId}-${n}` };
        },
        async complete(parts) {
          const u = uploads.get(uploadId);
          if (!u) throw new Error("no such upload");
          const sorted = [...parts].sort((a, b) => a.partNumber - b.partNumber);
          const chunks = sorted.map((p, i) => {
            const c = u.parts.get(p.partNumber);
            if (!c || p.etag !== `etag-${uploadId}-${p.partNumber}`) throw new Error("bad part");
            if (i < sorted.length - 1 && c.length < minPart) throw new Error("part too small");
            return c;
          });
          const data = new Uint8Array(chunks.reduce((s, c) => s + c.length, 0));
          let o = 0;
          for (const c of chunks) { data.set(c, o); o += c.length; }
          objects.set(key, { data, contentType: u.contentType });
          uploads.delete(uploadId);
          return {};
        },
        async abort() { uploads.delete(uploadId); },
      };
    },
    async head(key) {
      const o = objects.get(key);
      return o ? { size: o.data.length, httpMetadata: { contentType: o.contentType } } : null;
    },
    async get(key) {
      const o = objects.get(key);
      return o ? { size: o.data.length, body: new Response(o.data).body, httpMetadata: { contentType: o.contentType } } : null;
    },
    async delete(keys) { for (const k of Array.isArray(keys) ? keys : [keys]) objects.delete(k); },
  };
  return bucket;
}
