import type { HistoryFight } from "../types";
import { FighterName, fighterSideForName } from "./FighterName";

interface MatchupHistoryProps {
  nameA: string;
  nameB: string;
  priorMeetings: HistoryFight[];
  similarMatchups: HistoryFight[];
}

function fightWinnerName(fight: HistoryFight): string | null {
  if (fight.fighter1_won) return fight.fighter1_name;
  if (fight.fighter2_won) return fight.fighter2_name;
  return null;
}

function PriorMeetingWinnerAnnouncement({
  fight,
  nameA,
  nameB,
}: {
  fight: HistoryFight;
  nameA: string;
  nameB: string;
}) {
  const winner = fightWinnerName(fight);
  if (!winner) {
    return (
      <span className="history-winner">
        {" · "}
        <span className="history-winner-label">Winner:</span> N/A
      </span>
    );
  }

  const side = fighterSideForName(winner, nameA, nameB);

  return (
    <span className="history-winner">
      {" · "}
      <span className="history-winner-label">Winner:</span>{" "}
      {side ? (
        <FighterName side={side}>{winner}</FighterName>
      ) : (
        winner
      )}
    </span>
  );
}

function SimilarMatchupWinnerAnnouncement({
  fight,
  nameA,
  nameB,
}: {
  fight: HistoryFight;
  nameA: string;
  nameB: string;
}) {
  const winner = fightWinnerName(fight);
  if (!winner) {
    return (
      <span className="history-winner">
        {" · "}
        <span className="history-winner-label">Winner:</span> N/A
      </span>
    );
  }

  const side = fighterSideForName(winner, nameA, nameB);

  return (
    <span className="history-winner">
      {" · "}
      <span className="history-winner-label">Winner:</span>{" "}
      {side ? (
        <FighterName side={side}>{winner}</FighterName>
      ) : (
        winner
      )}
    </span>
  );
}

function PriorMeetingFighterName({
  name,
  nameA,
  nameB,
  won,
}: {
  name: string;
  nameA: string;
  nameB: string;
  won: boolean;
}) {
  if (!won) {
    return <span>{name}</span>;
  }

  const side = fighterSideForName(name, nameA, nameB);
  if (side) {
    return <FighterName side={side}>{name}</FighterName>;
  }

  return <span>{name}</span>;
}

function SimilarMatchupFighterName({
  name,
  nameA,
  nameB,
}: {
  name: string;
  nameA: string;
  nameB: string;
}) {
  const side = fighterSideForName(name, nameA, nameB);
  if (side) {
    return <FighterName side={side}>{name}</FighterName>;
  }
  return <span>{name}</span>;
}

function renderFighterName(
  variant: "prior" | "similar",
  name: string,
  nameA: string,
  nameB: string,
  won: boolean,
) {
  if (variant === "prior") {
    return (
      <PriorMeetingFighterName
        name={name}
        nameA={nameA}
        nameB={nameB}
        won={won}
      />
    );
  }

  return (
    <SimilarMatchupFighterName name={name} nameA={nameA} nameB={nameB} />
  );
}

function HistoryFightLine({
  fight,
  nameA,
  nameB,
  variant,
}: {
  fight: HistoryFight;
  nameA: string;
  nameB: string;
  variant: "prior" | "similar";
}) {
  return (
    <li className="history-item">
      {renderFighterName(
        variant,
        fight.fighter1_name,
        nameA,
        nameB,
        fight.fighter1_won,
      )}
      <span className="vs"> vs </span>
      {renderFighterName(
        variant,
        fight.fighter2_name,
        nameA,
        nameB,
        fight.fighter2_won,
      )}
      {fight.event_date && (
        <span className="history-meta"> ({fight.event_date})</span>
      )}
      {fight.event_name && (
        <span className="history-meta"> @ {fight.event_name}</span>
      )}
      {variant === "prior" ? (
        <PriorMeetingWinnerAnnouncement
          fight={fight}
          nameA={nameA}
          nameB={nameB}
        />
      ) : (
        <SimilarMatchupWinnerAnnouncement
          fight={fight}
          nameA={nameA}
          nameB={nameB}
        />
      )}
      {variant === "similar" && fight.similarity_pct != null && (
        <span className="similarity-tag">{fight.similarity_pct}% similarity</span>
      )}
    </li>
  );
}

export function MatchupHistory({
  nameA,
  nameB,
  priorMeetings,
  similarMatchups,
}: MatchupHistoryProps) {
  return (
    <>
      {priorMeetings.length > 0 && (
        <section className="history-section">
          <h3 className="section-title">Previous Meetings</h3>
          <ol className="history-list">
            {priorMeetings.map((fight, i) => (
              <HistoryFightLine
                key={i}
                fight={fight}
                nameA={nameA}
                nameB={nameB}
                variant="prior"
              />
            ))}
          </ol>
        </section>
      )}

      <section className="history-section">
        <h3 className="section-title">Comparable Historical Matchups</h3>
        <p className="section-subtitle">
          Based on pre-fight career stats and physical attributes
        </p>
        {similarMatchups.length === 0 ? (
          <p className="muted">
            There are no similar historical fights for this matchup.
          </p>
        ) : (
          <ol className="history-list">
            {similarMatchups.map((fight, i) => (
              <HistoryFightLine
                key={i}
                fight={fight}
                nameA={nameA}
                nameB={nameB}
                variant="similar"
              />
            ))}
          </ol>
        )}
      </section>
    </>
  );
}
