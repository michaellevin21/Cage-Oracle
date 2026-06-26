import type {
  MatchupHistory as HistoryData,
  MatchupMomentumBreakdown,
  MatchupResumeBreakdown,
  TaleOfTheTape,
} from "../types";
import { ComparisonTable } from "./ComparisonTable";
import { FighterName } from "./FighterName";
import { MatchupHistory } from "./MatchupHistory";
import { WinProbability } from "./WinProbability";

interface MatchupViewProps {
  tape: TaleOfTheTape;
  history: HistoryData;
  noPredictionReason: string | null;
  resumeBreakdown: MatchupResumeBreakdown;
  momentumBreakdown: MatchupMomentumBreakdown;
}

function weightClassLine(
  wcA: string | null,
  wcB: string | null,
): string | null {
  if (!wcA && !wcB) return null;
  if (wcA && wcA === wcB) return wcA;
  return `${wcA || "?"} / ${wcB || "?"}`;
}

export function MatchupView({
  tape,
  history,
  noPredictionReason,
  resumeBreakdown,
  momentumBreakdown,
}: MatchupViewProps) {
  const { fighter_a: fa, fighter_b: fb } = tape;
  const wc = weightClassLine(fa.weight_class, fb.weight_class);

  return (
    <div className="matchup-results">
      <header className="matchup-header">
        <p className="matchup-eyebrow">Tale of the Tape</p>
        <h2 className="matchup-title">
          <FighterName side="a">{fa.name}</FighterName>
          <span className="vs-divider">vs</span>
          <FighterName side="b">{fb.name}</FighterName>
        </h2>
        {wc && <p className="matchup-wc">{wc}</p>}
      </header>

      <WinProbability
        nameA={fa.name}
        nameB={fb.name}
        prediction={tape.prediction}
        noPredictionReason={noPredictionReason}
      />

      <ComparisonTable
        title="Profile"
        rows={tape.profile}
        nameA={fa.name}
        nameB={fb.name}
        resumeBreakdown={resumeBreakdown}
        momentumBreakdown={momentumBreakdown}
        expandOnHelp
      />

      {tape.archetype_summaries.length > 0 && (
        <section className="archetype-section">
          <h3 className="section-title">Historical Style Matchup</h3>
          <ul className="archetype-list">
            {tape.archetype_summaries.map((line, i) => (
              <li key={i}>{line}</li>
            ))}
          </ul>
        </section>
      )}

      <ComparisonTable
        title="Career (per-round & accuracy)"
        rows={tape.career}
        nameA={fa.name}
        nameB={fb.name}
      />

      <MatchupHistory
        nameA={fa.name}
        nameB={fb.name}
        priorMeetings={history.prior_meetings}
        similarMatchups={history.similar_matchups}
      />
    </div>
  );
}
