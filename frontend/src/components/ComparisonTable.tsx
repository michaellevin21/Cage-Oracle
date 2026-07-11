import { useState } from "react";
import type {
  ComparisonRow,
  MatchupMomentumBreakdown,
  MatchupResumeBreakdown,
  MomentumBreakdown,
  ResumeBreakdown,
} from "../types";
import { HelpPanelScope } from "./HelpPanelContext";
import { FighterName, fighterNameClass } from "./FighterName";
import { metricHelpFor } from "../metricHelpContent";
import { MomentumBreakdownHelp } from "./MomentumBreakdownHelp";
import { ResumeBreakdownHelp } from "./ResumeBreakdownHelp";
import { useScrollHint } from "./useScrollHint";

interface ComparisonTableProps {
  title: string;
  rows: ComparisonRow[];
  nameA: string;
  nameB: string;
  resumeBreakdown?: MatchupResumeBreakdown;
  momentumBreakdown?: MatchupMomentumBreakdown;
  expandOnHelp?: boolean;
}

const METRICS_WITHOUT_EDGE = new Set([
  "stance",
  "weight_class",
  "archetype",
  "career_rounds",
]);

function edgeLabel(row: ComparisonRow): string {
  if (METRICS_WITHOUT_EDGE.has(row.metric)) return "";
  return row.edge;
}

function edgeClass(row: ComparisonRow, side: "a" | "b"): string {
  if (METRICS_WITHOUT_EDGE.has(row.metric)) return "";
  if (row.edge === "Even") return "";
  const { advantage } = row;
  if (side === "a" && advantage === "fighter_a") return "cell-advantage-a";
  if (side === "b" && advantage === "fighter_b") return "cell-advantage-b";
  return "";
}

function edgeBadgeClass(row: ComparisonRow): string {
  if (row.edge === "Even") return "edge-badge";
  const { advantage } = row;
  if (advantage === "fighter_a") {
    return `edge-badge ${fighterNameClass("a")}`;
  }
  if (advantage === "fighter_b") {
    return `edge-badge ${fighterNameClass("b")}`;
  }
  return "edge-badge";
}

function archetypeDisplayValue(value: string): string {
  if (!value || value === "-") {
    return "N/A: Less than 3 rounds of fight data";
  }
  return value;
}

function momentumDisplayValue(breakdown: MomentumBreakdown): string {
  if (breakdown.score !== null) {
    return breakdown.score.toFixed(2);
  }
  if (breakdown.status === "inactive") {
    return "0.00";
  }
  return "-";
}

function resumeDisplayValue(breakdown: ResumeBreakdown): string {
  return String(breakdown.score);
}

function fighterCellValue(
  row: ComparisonRow,
  side: "a" | "b",
  fighterName: string,
  resumeBreakdown?: MatchupResumeBreakdown,
  momentumBreakdown?: MatchupMomentumBreakdown,
) {
  const value = side === "a" ? row.fighter_a : row.fighter_b;

  if (row.metric === "resume_score" && resumeBreakdown) {
    const breakdown =
      side === "a" ? resumeBreakdown.fighter_a : resumeBreakdown.fighter_b;
    return (
      <span className="metric-value-with-help">
        <span>{resumeDisplayValue(breakdown)}</span>
        <ResumeBreakdownHelp
          fighterName={fighterName}
          fighterSide={side}
          breakdown={breakdown}
        />
      </span>
    );
  }

  if (row.metric === "momentum_score" && momentumBreakdown) {
    const breakdown =
      side === "a"
        ? momentumBreakdown.fighter_a
        : momentumBreakdown.fighter_b;
    return (
      <span className="metric-value-with-help">
        <span>{momentumDisplayValue(breakdown)}</span>
        <MomentumBreakdownHelp
          fighterName={fighterName}
          fighterSide={side}
          breakdown={breakdown}
        />
      </span>
    );
  }

  if (row.metric === "archetype") {
    return archetypeDisplayValue(value);
  }

  return value;
}

export function ComparisonTable({
  title,
  rows,
  nameA,
  nameB,
  resumeBreakdown,
  momentumBreakdown,
  expandOnHelp = false,
}: ComparisonTableProps) {
  const [helpOpen, setHelpOpen] = useState(false);
  const { ref: scrollRef, overflowStart, overflowEnd } =
    useScrollHint<HTMLDivElement>();

  if (rows.length === 0) return null;

  const sectionClass = [
    "comparison-section",
    expandOnHelp && helpOpen ? "comparison-section-help-open" : "",
  ]
    .filter(Boolean)
    .join(" ");

  const content = (
    <section className={sectionClass}>
      <h3 className="section-title">{title}</h3>
      <div
        className="table-scroll"
        data-overflow-start={overflowStart}
        data-overflow-end={overflowEnd}
      >
        <div
          className="table-wrap"
          ref={scrollRef}
          tabIndex={0}
          role="region"
          aria-label={`${title} comparison, scroll horizontally to see more`}
        >
          <table className="comparison-table">
          <thead>
            <tr>
              <th className="col-metric">Metric</th>
              <th className="col-fighter">
                <FighterName side="a">{nameA}</FighterName>
              </th>
              <th className="col-fighter">
                <FighterName side="b">{nameB}</FighterName>
              </th>
              <th className="col-edge">Edge</th>
            </tr>
          </thead>
          <tbody>
            {rows.map((row) => (
              <tr key={row.metric}>
                <td className="col-metric">
                  {metricHelpFor(row.metric, row.label)}
                </td>
                <td className={`col-fighter ${edgeClass(row, "a")}`}>
                  {fighterCellValue(
                    row,
                    "a",
                    nameA,
                    resumeBreakdown,
                    momentumBreakdown,
                  )}
                </td>
                <td className={`col-fighter ${edgeClass(row, "b")}`}>
                  {fighterCellValue(
                    row,
                    "b",
                    nameB,
                    resumeBreakdown,
                    momentumBreakdown,
                  )}
                </td>
                <td className="col-edge">
                  {edgeLabel(row) && (
                    <span className={edgeBadgeClass(row)}>
                      {edgeLabel(row)}
                    </span>
                  )}
                </td>
              </tr>
            ))}
          </tbody>
          </table>
        </div>
        <span className="table-scroll-cue" aria-hidden="true">
          <span className="table-scroll-cue-chevron">›</span>
        </span>
      </div>
    </section>
  );

  if (!expandOnHelp) {
    return content;
  }

  return (
    <HelpPanelScope onAnyOpenChange={setHelpOpen}>{content}</HelpPanelScope>
  );
}
