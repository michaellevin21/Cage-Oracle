PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;

-- ─────────────────────────────────────────
-- Events
-- ─────────────────────────────────────────
CREATE TABLE IF NOT EXISTS events (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    ufc_event_id   TEXT    NOT NULL UNIQUE,
    name           TEXT    NOT NULL,
    location       TEXT,
    venue          TEXT,
    event_date     INTEGER NOT NULL  -- Unix timestamp
);

-- ─────────────────────────────────────────
-- Fighters
-- ─────────────────────────────────────────
CREATE TABLE IF NOT EXISTS fighters (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    ufc_id         TEXT    NOT NULL UNIQUE,
    name           TEXT    NOT NULL,
    stance         TEXT,                -- Orthodox, Southpaw, Switch
    reach_cm       INTEGER,
    height_cm      INTEGER,
    weight_class   TEXT,
    date_of_birth  INTEGER,             -- Unix timestamp
    status         TEXT,                -- Active, Retired, Released
    archetype      TEXT,                -- Populated in Week 5
    momentum_score REAL,                -- Populated in Week 7
    profile_url    TEXT,
    last_updated   INTEGER              -- Unix timestamp
);

-- ─────────────────────────────────────────
-- Fights
-- ─────────────────────────────────────────
CREATE TABLE IF NOT EXISTS fights (
    id                   INTEGER PRIMARY KEY AUTOINCREMENT,
    ufc_fight_id         TEXT    NOT NULL UNIQUE,
    fighter1_id          INTEGER NOT NULL REFERENCES fighters(id),
    fighter2_id          INTEGER NOT NULL REFERENCES fighters(id),
    event_id             INTEGER NOT NULL REFERENCES events(id),
    winner_id            INTEGER          REFERENCES fighters(id), -- NULL = draw/NC
    result_method        TEXT,            -- KO/TKO, SUB, DEC, DQ, NC, DRAW
    result_method_detail TEXT,            -- e.g. "Rear Naked Choke", "Punches"
    result_round         INTEGER,
    result_time_seconds  REAL,
    weight_class         TEXT,
    is_title_fight       INTEGER NOT NULL DEFAULT 0 CHECK (is_title_fight IN (0, 1)),
    fight_date           INTEGER NOT NULL -- Unix timestamp
);

-- ─────────────────────────────────────────
-- Round stats
-- ─────────────────────────────────────────
CREATE TABLE IF NOT EXISTS round_stats (
    id                        INTEGER PRIMARY KEY AUTOINCREMENT,
    fight_id                  INTEGER NOT NULL REFERENCES fights(id),
    fighter_id                INTEGER NOT NULL REFERENCES fighters(id),
    round_number              INTEGER NOT NULL CHECK (round_number BETWEEN 1 AND 5),
    sig_strikes_landed        INTEGER NOT NULL DEFAULT 0,
    sig_strikes_attempted     INTEGER NOT NULL DEFAULT 0,
    total_strikes_landed      INTEGER NOT NULL DEFAULT 0,
    total_strikes_attempted   INTEGER NOT NULL DEFAULT 0,
    takedowns_landed          INTEGER NOT NULL DEFAULT 0,
    takedowns_attempted       INTEGER NOT NULL DEFAULT 0,
    sub_attempts              INTEGER NOT NULL DEFAULT 0,
    reversals                 INTEGER NOT NULL DEFAULT 0,
    knockdowns                INTEGER NOT NULL DEFAULT 0,
    control_time_seconds      REAL    NOT NULL DEFAULT 0,
    head_strikes_landed       INTEGER NOT NULL DEFAULT 0,
    body_strikes_landed       INTEGER NOT NULL DEFAULT 0,
    leg_strikes_landed        INTEGER NOT NULL DEFAULT 0,
    distance_strikes_landed   INTEGER NOT NULL DEFAULT 0,
    clinch_strikes_landed     INTEGER NOT NULL DEFAULT 0,
    ground_strikes_landed     INTEGER NOT NULL DEFAULT 0,

    UNIQUE (fight_id, fighter_id, round_number)
);

-- ─────────────────────────────────────────
-- Fighter rankings
-- ─────────────────────────────────────────
CREATE TABLE IF NOT EXISTS fighter_rankings (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    fighter_id   INTEGER NOT NULL REFERENCES fighters(id),
    weight_class TEXT    NOT NULL,
    rank         INTEGER NOT NULL,  -- 0 = champion, 1-15 = contender
    updated_at   INTEGER NOT NULL,  -- Unix timestamp

    UNIQUE (fighter_id, weight_class)
);

-- ─────────────────────────────────────────
-- Indexes
-- ─────────────────────────────────────────

-- Fighter lookups by name (Week 3 query: get fighter by name)
CREATE INDEX IF NOT EXISTS idx_fighters_name
    ON fighters(name);

-- All fights for a given fighter
CREATE INDEX IF NOT EXISTS idx_fights_fighter1
    ON fights(fighter1_id);
CREATE INDEX IF NOT EXISTS idx_fights_fighter2
    ON fights(fighter2_id);

-- All fights on an event
CREATE INDEX IF NOT EXISTS idx_fights_event
    ON fights(event_id);

-- All round stats for a fight (most common join)
CREATE INDEX IF NOT EXISTS idx_round_stats_fight
    ON round_stats(fight_id);

-- All round stats for a fighter across their career
CREATE INDEX IF NOT EXISTS idx_round_stats_fighter
    ON round_stats(fighter_id);

-- Rankings lookup by weight class
CREATE INDEX IF NOT EXISTS idx_rankings_weight_class
    ON fighter_rankings(weight_class);
