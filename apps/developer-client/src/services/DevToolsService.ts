// DevToolsService - sends dev/test commands and reads world state from the game's dev server.
//
// Unlike ServerConnection (which holds SSE streams), this issues one-shot fetch() requests:
// the dev verbs (/api/dev/<verb>) run at the start of the game's next frame and answer with
// their result, and the state endpoint reads back a JSON snapshot after it renders
// (/api/state). Both are the game scene's: elsewhere they answer 404. The dev server sets
// CORS '*', so this works from the static file:// page (this is a static build, not a served
// app). Debug and RelWithDebInfo builds only: a Release build has no dev server.

export interface DevResult {
  ok: boolean;
  status: number;
  body: any;
}

export class DevToolsService {
  constructor(private serverUrl: string) {}

  /** Fire a dev verb: GET /api/dev/<verb>?<params>. Empty params are dropped. */
  async callDev(verb: string, params: Record<string, string | number> = {}): Promise<DevResult> {
    const query = Object.entries(params)
      .filter(([, v]) => v !== '' && v !== undefined && v !== null)
      .map(([k, v]) => `${encodeURIComponent(k)}=${encodeURIComponent(String(v))}`)
      .join('&');
    return this.fetchJson(`${this.serverUrl}/api/dev/${verb}${query ? `?${query}` : ''}`);
  }

  /** Read a world-state view: GET /api/state?what=<what>. */
  async getState(what: string): Promise<DevResult> {
    return this.fetchJson(`${this.serverUrl}/api/state?what=${encodeURIComponent(what)}`);
  }

  private async fetchJson(url: string): Promise<DevResult> {
    try {
      const res = await fetch(url);
      const body = await res.json().catch(() => ({}));
      return { ok: res.ok, status: res.status, body };
    } catch (e) {
      // Network error (game not running, wrong port). Surface it like a failed request.
      return { ok: false, status: 0, body: { error: String(e) } };
    }
  }
}

/** A colonist as serialized by /api/state?what=colonists. */
export interface ColonistState {
  id: number;
  name: string;
  x: number;
  y: number;
  needs: Record<string, number>;
  action?: string;
}
