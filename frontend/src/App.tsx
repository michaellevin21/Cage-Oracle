import { useEffect, useState, type SubmitEvent } from "react";
import { fetchMatchup } from "./api";
import { FighterSearch } from "./components/FighterSearch";
import { MatchupView } from "./components/MatchupView";
import { readMatchupFromUrl, writeMatchupToUrl } from "./matchupUrl";
import type { MatchupResponse } from "./types";

export default function App() {
  const [fighterA, setFighterA] = useState("");
  const [fighterB, setFighterB] = useState("");
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [result, setResult] = useState<MatchupResponse | null>(null);

  async function runAnalysis(nameA: string, nameB: string) {
    setLoading(true);
    setError(null);

    try {
      const data = await fetchMatchup(nameA, nameB);
      setResult(data);
      writeMatchupToUrl(nameA, nameB);
    } catch (err) {
      setResult(null);
      setError(err instanceof Error ? err.message : "Analysis failed.");
    } finally {
      setLoading(false);
    }
  }

  useEffect(() => {
    const { fighterA: urlA, fighterB: urlB } = readMatchupFromUrl();
    if (!urlA || !urlB || urlA === urlB) {
      return;
    }

    setFighterA(urlA);
    setFighterB(urlB);

    let cancelled = false;
    (async () => {
      setLoading(true);
      setError(null);
      try {
        const data = await fetchMatchup(urlA, urlB);
        if (!cancelled) {
          setResult(data);
        }
      } catch (err) {
        if (!cancelled) {
          setResult(null);
          setError(err instanceof Error ? err.message : "Analysis failed.");
        }
      } finally {
        if (!cancelled) {
          setLoading(false);
        }
      }
    })();

    return () => {
      cancelled = true;
    };
  }, []);

  function handleFighterA(name: string) {
    setFighterA(name);
  }

  function handleFighterB(name: string) {
    setFighterB(name);
  }

  const canAnalyze =
    fighterA.trim().length > 0 && fighterB.trim().length > 0;

  async function analyze(e: SubmitEvent<HTMLFormElement>) {
    e.preventDefault();

    if (!fighterA.trim() || !fighterB.trim()) {
      setError("Select two fighters to compare.");
      return;
    }
    if (fighterA.trim() === fighterB.trim()) {
      setError("Choose two different fighters.");
      return;
    }

    await runAnalysis(fighterA.trim(), fighterB.trim());
  }

  return (
    <div className="app">
      <header className="site-header">
        <h1>UFC Matchup Analyzer</h1>
        <p className="tagline">
          Side-by-side tale of the tape, style history, and win probability
        </p>
      </header>

      <main className="main">
        <form className="search-panel" onSubmit={analyze}>
          <div className="fighter-fields">
            <FighterSearch
              label="Fighter A"
              side="a"
              value={fighterA}
              onChange={handleFighterA}
              disabled={loading}
            />
            <div className="versus-badge" aria-hidden>
              VS
            </div>
            <FighterSearch
              label="Fighter B"
              side="b"
              value={fighterB}
              onChange={handleFighterB}
              disabled={loading}
            />
          </div>
          <button
            type="submit"
            className="analyze-btn"
            disabled={loading || !canAnalyze}
          >
            {loading ? "Analyzing…" : "Analyze Matchup"}
          </button>
        </form>

        {error && (
          <div className="error-banner" role="alert">
            {error}
          </div>
        )}

        {result && (
          <MatchupView
            tape={result.tape}
            history={result.history}
            noPredictionReason={result.no_prediction_reason}
            resumeBreakdown={result.resume_breakdown}
            momentumBreakdown={result.momentum_breakdown}
          />
        )}
      </main>
    </div>
  );
}
