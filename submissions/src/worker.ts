import { cleanup, createApp } from "./app";
import { d1Db } from "./db";
import type { Env } from "./env";

export default {
  fetch(req: Request, env: Env): Promise<Response> {
    return createApp({ db: d1Db(env.DB), bucket: env.FILES, env, now: Date.now })(req);
  },
  async scheduled(_event: ScheduledController, env: Env): Promise<void> {
    await cleanup({ db: d1Db(env.DB), bucket: env.FILES, now: Date.now });
  },
} satisfies ExportedHandler<Env>;
