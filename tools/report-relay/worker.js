// Solero bug/crash report relay - a Cloudflare Worker.
//
// Accepts the JSON payload Solero POSTs, requires a shared token header, caps the body
// size, does a light per-IP rate limit, then creates a GitHub issue with the token
// held as a Worker *secret* (so it never ships in the client). Returns { issueUrl }.
//
// Required config (see README.md):
//   Secret : GITHUB_TOKEN   (fine-grained PAT, Issues: read+write on the one repo)
//   Secret : SHARED_TOKEN   (matches Solero's kReportSharedToken)
//   Var    : REPO           (e.g. "owner/solero")
//   Optional KV binding: RL (namespace) for the rate limiter.

const MAX_BODY = 64 * 1024; // reject payloads larger than 64 KB
const RL_MAX = 5;           // max reports per IP per window
const RL_WINDOW = 600;      // seconds (10 min)

export default {
  async fetch(request, env) {
    if (request.method !== "POST")
      return json({ error: "POST only" }, 405);
    if (request.headers.get("X-Solero-Report") !== env.SHARED_TOKEN)
      return json({ error: "forbidden" }, 403);

    const raw = await request.text();
    if (raw.length > MAX_BODY)
      return json({ error: "payload too large" }, 413);

    // Simple per-IP rate limit (only if a KV namespace "RL" is bound).
    const ip = request.headers.get("CF-Connecting-IP") || "anon";
    if (env.RL) {
      const key = `rl:${ip}`;
      const n = parseInt((await env.RL.get(key)) || "0", 10);
      if (n >= RL_MAX) return json({ error: "rate limited, try later" }, 429);
      await env.RL.put(key, String(n + 1), { expirationTtl: RL_WINDOW });
    }

    let p;
    try { p = JSON.parse(raw); } catch { return json({ error: "bad json" }, 400); }
    if (!p || !p.title || !p.body) return json({ error: "missing fields" }, 400);

    const s = p.sections || {};
    const label = p.kind === "crash" ? "crash" : "bug";
    const body =
      `| field | value |\n|---|---|\n` +
      `| version | ${esc(s.version)} |\n` +
      `| os | ${esc(s.os)} |\n` +
      `| qt | ${esc(s.qt)} |\n` +
      `| deploy mode | ${esc(s.deployMode)} |\n` +
      `| mods | ${esc(s.modCount)} |\n\n` +
      `${p.body}\n\n` +
      `<details><summary>Log (home paths redacted)</summary>\n\n` +
      "```\n" + String(p.log || "").slice(0, 60000) + "\n```\n</details>\n";

    const resp = await fetch(`https://api.github.com/repos/${env.REPO}/issues`, {
      method: "POST",
      headers: {
        "Authorization": `Bearer ${env.GITHUB_TOKEN}`,
        "Accept": "application/vnd.github+json",
        "User-Agent": "solero-report-relay",
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ title: p.title, body, labels: [label] }),
    });

    if (!resp.ok)
      return json({ error: `github ${resp.status}` }, 502);
    const issue = await resp.json();
    return json({ issueUrl: issue.html_url }, 201);
  },
};

function json(obj, status) {
  return new Response(JSON.stringify(obj), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}

// Neutralise pipe/newline so a section value can't break the markdown table.
function esc(v) {
  return String(v == null ? "" : v).replace(/\|/g, "\\|").replace(/\n/g, " ");
}
