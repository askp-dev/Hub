/** The few calls the app needs, so tests can run on node:sqlite and production on D1. */
export interface Db {
  all<T>(sql: string, ...params: unknown[]): Promise<T[]>;
  first<T>(sql: string, ...params: unknown[]): Promise<T | null>;
  run(sql: string, ...params: unknown[]): Promise<{ changes: number }>;
}

export const cleanParams = (params: unknown[]) => params.map((p) => (p === undefined ? null : typeof p === "boolean" ? (p ? 1 : 0) : p));

export function d1Db(d1: D1Database): Db {
  return {
    async all<T>(sql: string, ...params: unknown[]) {
      return (await d1.prepare(sql).bind(...cleanParams(params)).all<T>()).results;
    },
    async first<T>(sql: string, ...params: unknown[]) {
      return (await d1.prepare(sql).bind(...cleanParams(params)).first<T>()) ?? null;
    },
    async run(sql: string, ...params: unknown[]) {
      return { changes: (await d1.prepare(sql).bind(...cleanParams(params)).run()).meta.changes ?? 0 };
    },
  };
}
