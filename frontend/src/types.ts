export interface FighterSummary {
  name: string;
  weight_class: string | null;
}

export interface ComparisonRow {
  metric: string;
  label: string;
  fighter_a: string;
  fighter_b: string;
  advantage: "fighter_a" | "fighter_b" | "tie" | null;
  edge: string;
}

export interface Prediction {
  type: "even" | "winner";
  winner_name: string | null;
  p_fighter_a: number;
  p_fighter_b: number;
  certainty_pct: number;
}

export interface TaleOfTheTape {
  fighter_a: { name: string; weight_class: string | null };
  fighter_b: { name: string; weight_class: string | null };
  profile: ComparisonRow[];
  career: ComparisonRow[];
  archetype_summaries: string[];
  prediction: Prediction | null;
}

export interface HistoryFight {
  fighter1_name: string;
  fighter2_name: string;
  fighter1_id: number | null;
  fighter2_id: number | null;
  winner_id: number | null;
  fighter1_won: boolean;
  fighter2_won: boolean;
  event_name: string;
  event_date: string | null;
  similarity: number | null;
  similarity_pct: number | null;
}

export interface MatchupHistory {
  prior_meetings: HistoryFight[];
  similar_matchups: HistoryFight[];
}

export interface ResumeWin {
  event_date: string;
  event_name: string;
  opponent_name: string;
  result_method: string;
  opponent_rank_label: string;
  points: number;
  running_total: number;
}

export interface ResumeBreakdown {
  score: number;
  ranked_wins: ResumeWin[];
  unranked_win_count: number;
  skipped_non_decisive: number;
}

export interface MatchupResumeBreakdown {
  fighter_a: ResumeBreakdown;
  fighter_b: ResumeBreakdown;
}

export interface MomentumFight {
  event_date: string;
  event_name: string;
  opponent_name: string;
  result: "W" | "L";
  result_method: string;
  opponent_rank_label: string;
  recency: number;
  opp_quality: number;
  finish_mult: number;
  contribution: number;
  weighted_contribution: number;
}

export interface MomentumBreakdown {
  status: "ok" | "no_fights" | "inactive" | "insufficient_fights";
  score: number | null;
  days_since_last_fight: number | null;
  min_decisive_fights: number;
  inactivity_days: number;
  fights: MomentumFight[];
  weighted_average?: number;
  finish_rate?: number;
  finish_boost?: number;
  adjusted?: number;
  neutral_score?: number;
  max_fight_contribution?: number;
}

export interface MatchupMomentumBreakdown {
  fighter_a: MomentumBreakdown;
  fighter_b: MomentumBreakdown;
}

export interface MatchupResponse {
  tape: TaleOfTheTape;
  history: MatchupHistory;
  no_prediction_reason: string | null;
  resume_breakdown: MatchupResumeBreakdown;
  momentum_breakdown: MatchupMomentumBreakdown;
}

export interface UpcomingMatchup {
  fighter_a: string;
  fighter_b: string;
  weight_class: string | null;
}

export interface UpcomingEvent {
  event_id: string;
  name: string;
  event_date: string | null;
  matchups: UpcomingMatchup[];
}

export interface UpcomingMatchupsResponse {
  events: UpcomingEvent[];
  detail?: string;
}
