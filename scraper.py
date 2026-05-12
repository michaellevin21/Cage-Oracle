"""
UFC Stats Scraper — single fighter page
Week 1: scrapes one fighter and inserts into SQLite

Usage:
    python scraper.py                        # scrapes Sean Strickland (default)
    python scraper.py 0d8011111be000b2       # scrapes by ufcstats fighter ID
"""

import sys
import sqlite3
import re
import time
from datetime import datetime
from dataclasses import dataclass, field

import requests
from bs4 import BeautifulSoup


# ─────────────────────────────────────────────────────────────
# Config
# ─────────────────────────────────────────────────────────────

BASE_URL    = "http://ufcstats.com"
DB_PATH     = "ufc.db"
HEADERS     = {"User-Agent": "UFC-Matchup-Analyzer/0.1 (personal project)"}
DELAY       = 1.5  # seconds between requests — be polite to the server


# ─────────────────────────────────────────────────────────────
# Data classes
# ─────────────────────────────────────────────────────────────

@dataclass
class Fighter:
    ufc_id:         str
    name:           str
    stance:         str | None
    reach_cm:       int | None
    height_cm:      int | None
    weight_class:   str | None
    date_of_birth:  int | None   # Unix timestamp
    status:         str | None
    profile_url:    str
    last_updated:   int          # Unix timestamp


@dataclass
class FightRow:
    ufc_fight_id:        str
    fighter1_ufc_id:     str
    fighter2_ufc_id:     str
    ufc_event_id:        str
    winner_ufc_id:       str | None
    result_method:       str | None
    result_method_detail:str | None
    result_round:        int | None
    result_time_seconds: float | None
    weight_class:        str | None
    is_title_fight:      int
    fight_date:          int | None  # Unix timestamp


@dataclass
class Event:
    ufc_event_id: str
    name:         str
    event_date:   int | None  # Unix timestamp
    location:     str | None = None
    venue:        str | None = None


# ─────────────────────────────────────────────────────────────
# Helpers
# ─────────────────────────────────────────────────────────────

def get_page(url: str) -> BeautifulSoup:
    """Fetch a page and return a BeautifulSoup object."""
    response = requests.get(url, headers=HEADERS, timeout=10)
    response.raise_for_status()
    time.sleep(DELAY)
    return BeautifulSoup(response.text, "html.parser")


def extract_id_from_url(url: str) -> str | None:
    """Pull the hex ID from a ufcstats URL, e.g. /fighter-details/0d8011111be000b2"""
    match = re.search(r'/([a-f0-9]{16})$', url)
    return match.group(1) if match else None


def feet_inches_to_cm(text: str) -> int | None:
    """Convert '6\' 1"' to centimetres."""
    match = re.search(r"(\d+)'\s*(\d+)\"", text)
    if not match:
        return None
    feet, inches = int(match.group(1)), int(match.group(2))
    return round((feet * 12 + inches) * 2.54)


def inches_to_cm(text: str) -> int | None:
    """Convert '76"' to centimetres."""
    match = re.search(r"(\d+)\"", text)
    return round(int(match.group(1)) * 2.54) if match else None


def parse_date_to_unix(text: str) -> int | None:
    """Convert 'Feb 27, 1991' or 'Feb. 27, 1991' to a Unix timestamp."""
    text = text.strip().replace(".", "")
    for fmt in ("%b %d, %Y", "%B %d, %Y"):
        try:
            return int(datetime.strptime(text, fmt).timestamp())
        except ValueError:
            continue
    return None


def parse_time_to_seconds(text: str) -> float | None:
    """Convert '4:33' to 273.0 seconds."""
    match = re.search(r"(\d+):(\d+)", text)
    if not match:
        return None
    return int(match.group(1)) * 60 + int(match.group(2))


def get_text(tag) -> str:
    """Return stripped inner text of a BeautifulSoup tag."""
    return tag.get_text(strip=True) if tag else ""


# ─────────────────────────────────────────────────────────────
# Scraping
# ─────────────────────────────────────────────────────────────

def scrape_fighter(fighter_id: str) -> tuple[Fighter, list[FightRow], list[Event]]:
    """
    Scrape a fighter's page and return:
      - Fighter dataclass
      - list of FightRow dataclasses
      - list of Event dataclasses (one per fight)
    """
    url  = f"{BASE_URL}/fighter-details/{fighter_id}"
    soup = get_page(url)

    # ── Fighter bio ──────────────────────────────────────────

    name = get_text(soup.find("span", class_="b-content__title-highlight"))

    bio_items = soup.select("ul.b-list__box-list li.b-list__box-list-item")
    bio = {}
    for item in bio_items:
        text = get_text(item)
        if ":" in text:
            key, _, val = text.partition(":")
            bio[key.strip().lower()] = val.strip()

    # Career record is in the page title, e.g. "Sean Strickland Record: 31-7-0"
    # Weight class isn't directly in the bio — we infer it from the fight history
    # (the fighter's weight class is whatever most of their fights were at)

    dob_raw = bio.get("dob", "")
    height_raw = bio.get("height", "")
    reach_raw  = bio.get("reach", "")
    stance     = bio.get("stance") or None

    fighter = Fighter(
        ufc_id        = fighter_id,
        name          = name,
        stance        = stance if stance and stance != "--" else None,
        reach_cm      = inches_to_cm(reach_raw) if reach_raw and reach_raw != "--" else None,
        height_cm     = feet_inches_to_cm(height_raw) if height_raw else None,
        weight_class  = None,   # populated below from fight history
        date_of_birth = parse_date_to_unix(dob_raw) if dob_raw and dob_raw != "--" else None,
        status        = "Active",  # assume active; Week 2 will handle retired detection
        profile_url   = url,
        last_updated  = int(datetime.now().timestamp()),
    )

    # ── Fight history ────────────────────────────────────────

    fights: list[FightRow] = []
    events: list[Event]    = []

    rows = soup.select("tr.b-fight-details__table-row[data-link]")

    for row in rows:
        cols = row.find_all("td")
        if len(cols) < 10:
            continue

        # Col 0: result (win/loss/draw/nc)
        result_tag = cols[0].find("a")
        result_text = get_text(result_tag).lower() if result_tag else ""

        # Col 1: fighters — two <a> tags
        fighter_links = cols[1].find_all("a")
        if len(fighter_links) < 2:
            continue

        f1_url = fighter_links[0].get("href", "")
        f2_url = fighter_links[1].get("href", "")
        f1_id  = extract_id_from_url(f1_url)
        f2_id  = extract_id_from_url(f2_url)
        if not f1_id or not f2_id:
            continue

        # Determine winner
        if result_text == "win":
            winner_id = f1_id
        elif result_text == "loss":
            winner_id = f2_id
        else:
            winner_id = None  # draw or NC

        # Col 5: event name + date
        event_tag  = cols[5].find("a")
        event_name = get_text(event_tag)
        event_url  = event_tag.get("href", "") if event_tag else ""
        event_id   = extract_id_from_url(event_url)

        date_tag  = cols[5].find("span")  # date is in a nested <span>
        date_text = get_text(date_tag)
        fight_date = parse_date_to_unix(date_text) if date_text else None

        # Title fight detection: ufcstats embeds a belt img in the event cell
        is_title_fight = 1 if cols[5].find("img") and "belt" in str(cols[5]) else 0

        # Col 6: method (e.g. "KO/TKO  Punches")
        method_lines = [t.strip() for t in cols[6].get_text(separator="\n").split("\n") if t.strip()]
        result_method        = method_lines[0] if method_lines else None
        result_method_detail = method_lines[1] if len(method_lines) > 1 else None

        # Normalise method strings
        if result_method:
            result_method = re.sub(r'\s+', ' ', result_method).strip()

        # Col 7: round
        round_text  = get_text(cols[7])
        result_round = int(round_text) if round_text.isdigit() else None

        # Col 8: time
        time_text = get_text(cols[8])
        result_time = parse_time_to_seconds(time_text)

        # Fight URL → fight ID
        fight_url = row.get("data-link", "")
        fight_id  = extract_id_from_url(fight_url)
        if not fight_id:
            continue

        # Weight class: ufcstats doesn't show it per row directly,
        # so we grab it from the fight details page later (Week 2).
        # For now, record None and patch in the bulk scraper.
        weight_class = None

        fights.append(FightRow(
            ufc_fight_id         = fight_id,
            fighter1_ufc_id      = f1_id,
            fighter2_ufc_id      = f2_id,
            ufc_event_id         = event_id,
            winner_ufc_id        = winner_id,
            result_method        = result_method,
            result_method_detail = result_method_detail,
            result_round         = result_round,
            result_time_seconds  = result_time,
            weight_class         = weight_class,
            is_title_fight       = is_title_fight,
            fight_date           = fight_date,
        ))

        if event_id:
            events.append(Event(
                ufc_event_id = event_id,
                name         = event_name,
                event_date   = fight_date,
            ))

    # Infer fighter weight class from most common weight class in fights
    # (for now just leave it None; patch in Week 2 when we have full fight data)
    # fighter.weight_class = most_common_weight_class(fights)

    return fighter, fights, events


# ─────────────────────────────────────────────────────────────
# Database
# ─────────────────────────────────────────────────────────────

def get_connection(db_path: str) -> sqlite3.Connection:
    conn = sqlite3.connect(db_path)
    conn.execute("PRAGMA foreign_keys = ON")
    conn.execute("PRAGMA journal_mode = WAL")
    conn.row_factory = sqlite3.Row
    return conn


def insert_fighter(conn: sqlite3.Connection, f: Fighter) -> int:
    """Insert or update a fighter. Returns the row's internal id."""
    conn.execute("""
        INSERT INTO fighters
            (ufc_id, name, stance, reach_cm, height_cm,
             weight_class, date_of_birth, status, profile_url, last_updated)
        VALUES (?,?,?,?,?,?,?,?,?,?)
        ON CONFLICT(ufc_id) DO UPDATE SET
            name          = excluded.name,
            stance        = excluded.stance,
            reach_cm      = excluded.reach_cm,
            height_cm     = excluded.height_cm,
            date_of_birth = excluded.date_of_birth,
            status        = excluded.status,
            profile_url   = excluded.profile_url,
            last_updated  = excluded.last_updated
    """, (
        f.ufc_id, f.name, f.stance,
        f.reach_cm, f.height_cm, f.weight_class, f.date_of_birth,
        f.status, f.profile_url, f.last_updated,
    ))
    row = conn.execute("SELECT id FROM fighters WHERE ufc_id = ?", (f.ufc_id,)).fetchone()
    return row["id"]


def insert_event(conn: sqlite3.Connection, e: Event) -> int:
    """Insert or ignore an event. Returns the row's internal id."""
    conn.execute("""
        INSERT INTO events (ufc_event_id, name, location, venue, event_date)
        VALUES (?,?,?,?,?)
        ON CONFLICT(ufc_event_id) DO NOTHING
    """, (e.ufc_event_id, e.name, e.location, e.venue, e.event_date))
    row = conn.execute("SELECT id FROM events WHERE ufc_event_id = ?", (e.ufc_event_id,)).fetchone()
    return row["id"]


def ensure_placeholder_fighter(conn: sqlite3.Connection, ufc_id: str) -> int:
    """
    Insert a minimal placeholder row for an opponent so FK constraints pass.
    The bulk scraper (Week 2) will fill in the full details later.
    """
    conn.execute("""
        INSERT INTO fighters (ufc_id, name, last_updated)
        VALUES (?, '[unknown]', ?)
        ON CONFLICT(ufc_id) DO NOTHING
    """, (ufc_id, int(datetime.now().timestamp())))
    row = conn.execute("SELECT id FROM fighters WHERE ufc_id = ?", (ufc_id,)).fetchone()
    return row["id"]


def insert_fight(conn: sqlite3.Connection, fight: FightRow,
                 fighter_id_map: dict[str, int], event_id_map: dict[str, int]) -> None:
    """Insert or ignore a fight row."""
    f1_id     = fighter_id_map.get(fight.fighter1_ufc_id)
    f2_id     = fighter_id_map.get(fight.fighter2_ufc_id)
    event_id  = event_id_map.get(fight.ufc_event_id)
    winner_id = fighter_id_map.get(fight.winner_ufc_id) if fight.winner_ufc_id else None

    if not f1_id or not f2_id or not event_id:
        print(f"  [skip] fight {fight.ufc_fight_id} — missing FK reference")
        return

    conn.execute("""
        INSERT INTO fights
            (ufc_fight_id, fighter1_id, fighter2_id, event_id, winner_id,
             result_method, result_method_detail, result_round,
             result_time_seconds, weight_class, is_title_fight, fight_date)
        VALUES (?,?,?,?,?,?,?,?,?,?,?,?)
        ON CONFLICT(ufc_fight_id) DO NOTHING
    """, (
        fight.ufc_fight_id, f1_id, f2_id, event_id, winner_id,
        fight.result_method, fight.result_method_detail, fight.result_round,
        fight.result_time_seconds, fight.weight_class,
        fight.is_title_fight, fight.fight_date,
    ))


# ─────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────

def main():
    fighter_id = sys.argv[1] if len(sys.argv) > 1 else "0d8011111be000b2"  # Sean Strickland

    print(f"Scraping fighter: {fighter_id}")
    fighter, fights, events = scrape_fighter(fighter_id)
    print(f"  Found: {fighter.name} — {len(fights)} fights, {len(events)} events")

    conn = get_connection(DB_PATH)

    with conn:
        # 1. Insert the main fighter
        main_fighter_db_id = insert_fighter(conn, fighter)
        fighter_id_map = {fighter.ufc_id: main_fighter_db_id}

        # 2. Insert all events
        event_id_map: dict[str, int] = {}
        for event in events:
            if event.ufc_event_id and event.ufc_event_id not in event_id_map:
                db_id = insert_event(conn, event)
                event_id_map[event.ufc_event_id] = db_id

        # 3. Ensure placeholder rows exist for every opponent
        opponent_ids = set()
        for fight in fights:
            for uid in (fight.fighter1_ufc_id, fight.fighter2_ufc_id):
                if uid != fighter_id:
                    opponent_ids.add(uid)

        for opp_id in opponent_ids:
            db_id = ensure_placeholder_fighter(conn, opp_id)
            fighter_id_map[opp_id] = db_id

        # 4. Insert fights
        for fight in fights:
            insert_fight(conn, fight, fighter_id_map, event_id_map)

    conn.close()

    # ── Print summary ────────────────────────────────────────
    print()
    print(f"{'─' * 50}")
    print(f"  {fighter.name}")
    print(f"{'─' * 50}")
    print(f"  Stance:    {fighter.stance or 'N/A'}")
    print(f"  Height:    {fighter.height_cm} cm" if fighter.height_cm else "  Height:    N/A")
    print(f"  Reach:     {fighter.reach_cm} cm"  if fighter.reach_cm  else "  Reach:     N/A")
    dob = datetime.fromtimestamp(fighter.date_of_birth).strftime("%b %d, %Y") if fighter.date_of_birth else "N/A"
    print(f"  DOB:       {dob}")
    print(f"  Fights:    {len(fights)} scraped")
    print(f"  Saved to:  {DB_PATH}")
    print(f"{'─' * 50}")


if __name__ == "__main__":
    main()
