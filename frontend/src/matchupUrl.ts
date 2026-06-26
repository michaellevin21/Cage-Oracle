export function readMatchupFromUrl(): { fighterA: string; fighterB: string } {
  const params = new URLSearchParams(window.location.search);
  return {
    fighterA: params.get("fighter_a")?.trim() ?? "",
    fighterB: params.get("fighter_b")?.trim() ?? "",
  };
}

export function writeMatchupToUrl(fighterA: string, fighterB: string): void {
  const url = new URL(window.location.href);
  url.searchParams.set("fighter_a", fighterA);
  url.searchParams.set("fighter_b", fighterB);
  window.history.replaceState(null, "", url);
}
