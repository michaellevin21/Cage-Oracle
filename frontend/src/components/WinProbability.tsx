import type { Prediction } from "../types";
import { FighterName } from "./FighterName";

interface WinProbabilityProps {
  nameA: string;
  nameB: string;
  prediction: Prediction | null;
  noPredictionReason: string | null;
}

export function WinProbability({
  nameA,
  nameB,
  prediction,
  noPredictionReason,
}: WinProbabilityProps) {
  if (!prediction) {
    if (!noPredictionReason) return null;
    return (
      <section className="prediction-card prediction-muted">
        <p>{noPredictionReason}</p>
      </section>
    );
  }

  const pctA = Math.round(prediction.p_fighter_a * 1000) / 10;
  const pctB = Math.round(prediction.p_fighter_b * 1000) / 10;
  const winnerIsA = prediction.winner_name === nameA;

  return (
    <section className="prediction-card">
      <h3 className="section-title">Win Probability</h3>
      {prediction.type === "even" ? (
        <p className="prediction-headline">Even matchup ({pctA}%)</p>
      ) : (
        <p className="prediction-headline">
          Predicted winner:{" "}
          <strong>
            <FighterName side={winnerIsA ? "a" : "b"}>
              {prediction.winner_name}
            </FighterName>
          </strong>{" "}
          ({prediction.certainty_pct.toFixed(1)}% certainty)
        </p>
      )}
      <div className="prob-bar">
        <div
          className="prob-segment prob-a"
          style={{ width: `${pctA}%` }}
          title={`${nameA}: ${pctA}%`}
        />
        <div
          className="prob-segment prob-b"
          style={{ width: `${pctB}%` }}
          title={`${nameB}: ${pctB}%`}
        />
      </div>
      <div className="prob-labels">
        <span>
          <FighterName side="a">{nameA}</FighterName> <strong>{pctA}%</strong>
        </span>
        <span>
          <FighterName side="b">{nameB}</FighterName> <strong>{pctB}%</strong>
        </span>
      </div>
    </section>
  );
}
