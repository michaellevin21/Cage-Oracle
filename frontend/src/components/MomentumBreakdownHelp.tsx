import type { MomentumBreakdown } from "../types";
import { MetricHelp } from "./MetricHelp";

interface MomentumBreakdownHelpProps {
  fighterName: string;
  fighterSide: "a" | "b";
  breakdown: MomentumBreakdown;
}

function statusMessage(breakdown: MomentumBreakdown): string | null {
  switch (breakdown.status) {
    case "no_fights":
      return "No fights on record — momentum cannot be computed.";
    case "inactive":
      return `Inactive — last fight was ${Math.floor(breakdown.days_since_last_fight ?? 0)} days ago (limit: ${breakdown.inactivity_days} days). Score is 0.`;
    case "insufficient_fights":
      return `Not enough decisive fights (${breakdown.fights.length}/${breakdown.min_decisive_fights} required).`;
    default:
      return null;
  }
}

export function MomentumBreakdownHelp({
  fighterName,
  fighterSide,
  breakdown,
}: MomentumBreakdownHelpProps) {
  const status = statusMessage(breakdown);

  return (
    <MetricHelp
      title={`${fighterName} — Momentum`}
      wide
      titleTone={fighterSide}
    >
      {breakdown.score !== null && (
        <p>
          Score: <strong>{breakdown.score.toFixed(2)}</strong> / 100
        </p>
      )}
      {status && <p>{status}</p>}
      {breakdown.status === "ok" && (
        <p className="metric-help-formula">
          50 + ({breakdown.adjusted!.toFixed(3)} /{" "}
          {breakdown.max_fight_contribution}) × 50, after a finish-rate boost of{" "}
          {breakdown.finish_boost!.toFixed(3)}×
        </p>
      )}
      {breakdown.fights.length > 0 && (
        <div className="resume-breakdown-table-wrap">
          <table className="resume-breakdown-table momentum-breakdown-table">
            <thead>
              <tr>
                <th>Date</th>
                <th>Opponent</th>
                <th>Rslt</th>
                <th>Rec</th>
                <th>Qual</th>
                <th>Wtd</th>
              </tr>
            </thead>
            <tbody>
              {breakdown.fights.map((fight, i) => (
                <tr key={i}>
                  <td>{fight.event_date}</td>
                  <td>{fight.opponent_name}</td>
                  <td>{fight.result}</td>
                  <td>{fight.recency.toFixed(2)}</td>
                  <td>{fight.opp_quality.toFixed(2)}</td>
                  <td>{fight.weighted_contribution.toFixed(2)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
      {breakdown.status === "ok" && (
        <p className="metric-help-footnote">
          Rec = recency weight (1.0 for first year, then decays). Qual = opponent
          quality from current rankings (unranked 0.35, champion 1.00, #1–#15
          scaled). Wtd = weighted contribution (wins: quality × finish bonus x recency;
          losses: −1.00 × finish bonus × recency).
          Finish bonus = 1.30× on KO/TKO/SUB results.
        </p>
      )}
    </MetricHelp>
  );
}
