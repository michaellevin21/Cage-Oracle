import type { ResumeBreakdown } from "../types";
import { MetricHelp } from "./MetricHelp";

interface ResumeBreakdownHelpProps {
  fighterName: string;
  fighterSide: "a" | "b";
  breakdown: ResumeBreakdown;
}

export function ResumeBreakdownHelp({
  fighterName,
  fighterSide,
  breakdown,
}: ResumeBreakdownHelpProps) {
  return (
    <MetricHelp
      title={`${fighterName} — Resume`}
      wide
      titleTone={fighterSide}
    >
      <p>
        Total: <strong>{breakdown.score}</strong> points
      </p>
      {breakdown.ranked_wins.length > 0 ? (
        <div className="resume-breakdown-table-wrap">
          <table className="resume-breakdown-table">
            <thead>
              <tr>
                <th>Date</th>
                <th>Opponent</th>
                <th>Rank</th>
                <th>Pts</th>
              </tr>
            </thead>
            <tbody>
              {breakdown.ranked_wins.map((win, i) => (
                <tr key={i}>
                  <td>{win.event_date}</td>
                  <td>{win.opponent_name}</td>
                  <td>{win.opponent_rank_label}</td>
                  <td>{win.points}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      ) : (
        <p>No wins over currently ranked opponents.</p>
      )}
      {(breakdown.unranked_win_count > 0 ||
        breakdown.skipped_non_decisive > 0) && (
        <p className="metric-help-footnote">
          {breakdown.unranked_win_count > 0 &&
            `${breakdown.unranked_win_count} win(s) over unranked opponents (0 pts each)`}
          {breakdown.unranked_win_count > 0 &&
            breakdown.skipped_non_decisive > 0 &&
            "; "}
          {breakdown.skipped_non_decisive > 0 &&
            `${breakdown.skipped_non_decisive} non-decisive result(s) excluded`}
        </p>
      )}
    </MetricHelp>
  );
}
