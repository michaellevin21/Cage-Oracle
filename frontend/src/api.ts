import type { FighterSummary, MatchupResponse } from "./types";

async function apiFetch<T>(path: string): Promise<T> {
  const res = await fetch(path);
  if (!res.ok) {
    let detail = res.statusText;
    try {
      const body = await res.json();
      if (body?.detail) detail = String(body.detail);
    } catch {
      /* ignore */
    }
    throw new Error(detail);
  }
  return res.json() as Promise<T>;
}

export function searchFighters(query: string): Promise<FighterSummary[]> {
  const params = new URLSearchParams({ q: query });
  return apiFetch(`/api/fighters/search?${params}`);
}

export function fetchMatchup(
  fighterA: string,
  fighterB: string,
): Promise<MatchupResponse> {
  const params = new URLSearchParams({
    fighter_a: fighterA,
    fighter_b: fighterB,
  });
  return apiFetch(`/api/matchup?${params}`);
}
