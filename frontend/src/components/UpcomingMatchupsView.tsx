import { useEffect, useState } from "react";
import { fetchUpcomingMatchups } from "../api";
import type { UpcomingEvent, UpcomingMatchup } from "../types";

interface UpcomingMatchupsViewProps {
  onSelectMatchup: (fighterA: string, fighterB: string) => void;
  disabled?: boolean;
}

export function UpcomingMatchupsView({
  onSelectMatchup,
  disabled = false,
}: UpcomingMatchupsViewProps) {
  const [events, setEvents] = useState<UpcomingEvent[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;

    (async () => {
      setLoading(true);
      setError(null);
      try {
        const data = await fetchUpcomingMatchups();
        if (cancelled) {
          return;
        }
        if (data.detail) {
          setError(data.detail);
          setEvents([]);
          return;
        }
        setEvents(data.events);
        if (data.events.length === 0) {
          setError("No upcoming UFC fights found.");
        }
      } catch (err) {
        if (!cancelled) {
          setEvents([]);
          setError(
            err instanceof Error
              ? err.message
              : "Failed to load upcoming matchups.",
          );
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

  function handleSelect(matchup: UpcomingMatchup) {
    if (disabled) {
      return;
    }
    onSelectMatchup(matchup.fighter_a, matchup.fighter_b);
  }

  if (loading) {
    return (
      <div className="upcoming-panel">
        <p className="upcoming-status">Loading upcoming fights…</p>
      </div>
    );
  }

  if (error) {
    return (
      <div className="upcoming-panel">
        <p className="upcoming-status error-text" role="alert">
          {error}
        </p>
      </div>
    );
  }

  return (
    <div className="upcoming-panel">
      <p className="upcoming-intro">
        Select a bout to open the full matchup breakdown.
      </p>
      <div className="upcoming-events">
        {events.map((event) => (
          <section key={event.event_id} className="upcoming-event">
            <header className="upcoming-event-header">
              <h2 className="upcoming-event-name">{event.name}</h2>
              <p className="upcoming-event-date">
                {event.event_date ?? "Date TBD"}
              </p>
            </header>
            <ul className="upcoming-matchup-list">
              {event.matchups.map((matchup) => {
                const key = `${matchup.fighter_a}|${matchup.fighter_b}`;
                return (
                  <li key={key}>
                    <button
                      type="button"
                      className="upcoming-matchup-btn"
                      onClick={() => handleSelect(matchup)}
                      disabled={disabled}
                    >
                      <span className="upcoming-fighters">
                        <span className="upcoming-fighter-a">
                          {matchup.fighter_a}
                        </span>
                        <span className="upcoming-vs">vs</span>
                        <span className="upcoming-fighter-b">
                          {matchup.fighter_b}
                        </span>
                      </span>
                      {matchup.weight_class && (
                        <span className="upcoming-weight-class">
                          {matchup.weight_class}
                        </span>
                      )}
                    </button>
                  </li>
                );
              })}
            </ul>
          </section>
        ))}
      </div>
    </div>
  );
}
