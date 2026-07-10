import { MetricHelp } from "./components/MetricHelp";

const MOMENTUM_HELP = (
  <>
    <p>
      A 0–100 score summarizing how a fighter has been performing recently.{" "}
      <strong>50 is neutral</strong> — above 50 suggests positive momentum,
      below 50 suggests declining form.
    </p>
    <p>
      It is calculated from up to the last 5 decisive fights (wins or losses). At least 3 are required; fighters inactive for 2+ years score
      0.
    </p>
    <ul>
      <li>
        Opponent quality (Qual) comes from the opponent&apos;s{" "}
        <strong>current</strong> ranking: unranked = 0.35, champion = 1.00,
        #1–#15 scale down from there.
      </li>
      <li>
        Wins scale with opponent quality; losses use the same fixed penalty
        regardless of who you lost to
      </li>
      <li>
        More recent fights are weighted more heavily (full weight for 1 year,
        then gradual decay)
      </li>
      <li>A 1.30× multiplier applies to KO/TKO/submission wins and losses</li>
    </ul>
    <p>
      Use the <strong>?</strong> next to each fighter&apos;s score for their
      specific calculation breakdown.
    </p>
  </>
);

const RESUME_HELP = (
  <>
    <p>
      A point total measuring the quality of a fighter&apos;s resume based on{" "}
      <strong>career wins over currently ranked opponents</strong>.
    </p>
    <p>
      Each ranked win earns points from the opponent&apos;s best current
      weight-class rank (pound-for-pound rankings are ignored):
    </p>
    <ul>
      <li>Champion = 16 pts, #1 = 15, #2 = 14, …, #15 = 1</li>
      <li>Wins over unranked opponents count for 0 points</li>
      <li>Beating the same ranked opponent multiple times adds points each time</li>
    </ul>
    <p>
      Use the <strong>?</strong> next to each fighter&apos;s score for their
      specific win-by-win breakdown.
    </p>
  </>
);

const ARCHETYPE_HELP = (
  <>
    <p>
      A fighting-style label derived from a fighter&apos;s{" "}
      <strong>career round by round stats</strong> — striking pace, grappling, control
      time, and where they do their damage.
    </p>
    <ul className="archetype-help-list">
      <li>
        <strong>Pressure Striker</strong> — Works at range with heavy distance
        striking; limited grappling output.
      </li>
      <li>
        <strong>Control Wrestler</strong> — Ground-control first; racks up top
        control time rather than hunting for finishes.
      </li>
      <li>
        <strong>Ground Finisher</strong> — Submission and/or ground and pound threat with extended
        control and ground offense.
      </li>
      <li>
        <strong>All-Around Fighter</strong> — Mixes striking with wrestling
        and clinch pressure.
      </li>
      <li>
        <strong>Counter Striker</strong> — Defensive striker — strong defense,
        lower volume absorbed.
      </li>
    </ul>
    <p>
      Archetype matchups feed the historical style analysis shown below the
      profile when data is available.
    </p>
  </>
);

const METRIC_HELP: Record<string, { title: string; content: React.ReactNode; wide?: boolean }> =
  {
    momentum_score: { title: "Momentum", content: MOMENTUM_HELP },
    resume_score: { title: "Resume", content: RESUME_HELP },
    archetype: { title: "Archetype", content: ARCHETYPE_HELP, wide: true },
  };

export function metricHelpFor(metric: string, label: string) {
  const help = METRIC_HELP[metric];
  if (!help) {
    return label;
  }

  return (
    <span className="metric-label">
      {label}
      <MetricHelp title={help.title} wide={help.wide}>
        {help.content}
      </MetricHelp>
    </span>
  );
}
