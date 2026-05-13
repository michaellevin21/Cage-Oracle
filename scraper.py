"""
UFC Stats Scraper - fighter pages, fight details, and SQLite

Writes fighters, events, fights, round_stats (from ufcstats fight pages once per fight), and
fighter_rankings (from ufc.com official rankings - name-matched to DB).

Usage:
    python scraper.py                    # full catalog: all fighters + all fights (slow)
    python scraper.py 0d8011111be000b2   # single fighter by ufcstats id
    python scraper.py --limit 12 --delay 0.5   # quick test: first 12 fighters in directory only
    python scraper.py --delay 2.0        # be kinder to the server between requests
"""

import argparse
import sqlite3
import re
import time
from datetime import datetime
from dataclasses import dataclass

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


@dataclass
class Event:
    ufc_event_id: str
    name:         str
    event_date:   int | None  # Unix timestamp


@dataclass
class RoundStatEntry:
    fighter_ufc_id: str
    round_number: int
    sig_strikes_landed: int
    sig_strikes_attempted: int
    total_strikes_landed: int
    total_strikes_attempted: int
    takedowns_landed: int
    takedowns_attempted: int
    sub_attempts: int
    reversals: int
    knockdowns: int
    control_time_seconds: float
    head_strikes_landed: int
    body_strikes_landed: int
    leg_strikes_landed: int
    distance_strikes_landed: int
    clinch_strikes_landed: int
    ground_strikes_landed: int


# UFC.com hosts official rankings; ufcstats.com does not.
UFC_RANKINGS_URL = "https://www.ufc.com/rankings"

def get_page(url: str) -> BeautifulSoup:
    """Fetch a page and return a BeautifulSoup object."""
    response = requests.get(url, headers=HEADERS, timeout=10)
    response.raise_for_status()
    time.sleep(DELAY)
    return BeautifulSoup(response.text, "html.parser")


def extract_id_from_url(url: str) -> str | None:
    """Pull the 16-char hex id from a ufcstats URL (fighter-details, event-details, etc.)."""
    match = re.search(r"/([a-f0-9]{16})(?:\?|#|$)", url, re.I)
    return match.group(1).lower() if match else None


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


def parse_fighter_weight_lbs_from_soup(soup: BeautifulSoup) -> int | None:
    """Pounds from the fighter profile weight line (info box or list)."""
    box = soup.select_one("div.b-list__info-box.js-guide")
    if box:
        m = re.search(r"Weight:\s*(\d+)\s*lbs", box.get_text(separator=" ", strip=True), re.I)
        if m:
            return int(m.group(1))
    for li in soup.select("li.b-list__box-list-item"):
        t = li.get_text(strip=True)
        if not t.lower().startswith("weight"):
            continue
        m = re.search(r"Weight:\s*(\d+)\s*lbs", t, re.I)
        if m:
            return int(m.group(1))
        m = re.search(r"(\d+)\s*lbs", t, re.I)
        if m:
            return int(m.group(1))
    return None


def _fighter_page_implies_womens(soup: BeautifulSoup) -> bool:
    """Heuristic: women's divisions often appear in page copy."""
    sample = soup.get_text(separator=" ", strip=False)[:25000]
    return bool(re.search(r"Women'?s\s+(?:Strawweight|Flyweight|Bantamweight|Featherweight)", sample, re.I))


def lbs_to_fighter_weight_class(lbs: int, womens: bool) -> str | None:
    """Map weigh-in pounds to a UFC-style division label (aligned with fight-card wording)."""
    if lbs < 90 or lbs > 400:
        return None
    if womens:
        if lbs <= 115:
            return "Women's Strawweight"
        if lbs <= 125:
            return "Women's Flyweight"
        if lbs <= 135:
            return "Women's Bantamweight"
        if lbs <= 145:
            return "Women's Featherweight"
        return None
    if lbs >= 206:
        return "Heavyweight"
    if lbs >= 186:
        return "Light Heavyweight"
    if lbs >= 171:
        return "Middleweight"
    if lbs >= 156:
        return "Welterweight"
    if lbs >= 146:
        return "Lightweight"
    if lbs >= 136:
        return "Featherweight"
    if lbs >= 126:
        return "Bantamweight"
    if lbs >= 116:
        return "Flyweight"
    return None


def derive_weight_class_from_fighter_page(soup: BeautifulSoup) -> str | None:
    lbs = parse_fighter_weight_lbs_from_soup(soup)
    if lbs is None:
        return None
    return lbs_to_fighter_weight_class(lbs, _fighter_page_implies_womens(soup))


def parse_weight_class_from_fight_soup(soup: BeautifulSoup) -> str | None:
    """
    Parse the fight-head subtitle, e.g.:
      'UFC Middleweight Title Bout' -> 'Middleweight'
      'Middleweight Bout' (non-title) -> 'Middleweight'
    """
    title_i = soup.select_one("i.b-fight-details__fight-title")
    if not title_i:
        return None
    raw = re.sub(r"\s+", " ", title_i.get_text(separator=" ", strip=True))
    for pat in (
        r"^UFC\s+(.+?)(?:\s+Title)?\s+Bout\s*$",
        r"^(.+?)(?:\s+Title)?\s+Bout\s*$",
    ):
        m = re.match(pat, raw, re.I)
        if m:
            label = m.group(1).strip()
            return label or None
    return None


def get_text(tag) -> str:
    """Return stripped inner text of a BeautifulSoup tag."""
    return tag.get_text(strip=True) if tag else ""


def extract_event_date_text_from_cell(cell) -> str:
    """
    Fight-history event column: date used to live in <span>; current markup uses <p>
    (often with a belt <img>) after the event-name link.
    """
    span = cell.find("span")
    if span:
        t = get_text(span)
        if t:
            return t
    # Prefer a paragraph whose text looks like "Mon. DD, YYYY" / "Month DD, YYYY"
    for p in reversed(cell.find_all("p")):
        t = get_text(p)
        if not t:
            continue
        if re.search(r"\b(19|20)\d{2}\b", t) and re.search(
            r"\b(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)[a-z]*\.?\s+\d",
            t,
            re.I,
        ):
            return t
    return ""


def parse_landed_attempted(text: str) -> tuple[int, int]:
    """Parse '10 of 19' or '---' into (landed, attempted)."""
    text = (text or "").strip().replace("\xa0", " ")
    if not text or text in ("---", "--"):
        return 0, 0
    m = re.match(r"(\d+)\s*of\s*(\d+)", text, re.I)
    if not m:
        return 0, 0
    return int(m.group(1)), int(m.group(2))


def parse_landed_only(text: str) -> int:
    return parse_landed_attempted(text)[0]


def parse_control_seconds(text: str) -> float:
    """Parse '3:01' (M:SS) to seconds."""
    text = (text or "").strip()
    if not text or text in ("---", "--"):
        return 0.0
    m = re.match(r"(\d+):(\d+)$", text)
    if not m:
        return 0.0
    return int(m.group(1)) * 60.0 + int(m.group(2))


def parse_int_cell(text: str) -> int:
    text = (text or "").strip()
    if not text or text in ("---", "--"):
        return 0
    try:
        return int(text)
    except ValueError:
        return 0


def _per_round_totals_table(soup: BeautifulSoup):
    for sec in soup.select(".js-fight-section"):
        if "per round" not in sec.get_text(strip=True).lower():
            continue
        tbl = sec.find("table", class_="b-fight-details__table") or sec.find("table")
        if not tbl:
            continue
        thead = tbl.find("thead")
        if not thead:
            continue
        heads = [th.get_text(strip=True) for th in thead.find_all("th")]
        if "KD" in heads and "Total str." in heads and "Ctrl" in heads and "Head" not in heads:
            body = tbl.find("tbody")
            if body and body.find_all("tr"):
                return tbl
    return None


def _per_round_sig_target_table(soup: BeautifulSoup):
    for sec in soup.select(".js-fight-section"):
        if "per round" not in sec.get_text(strip=True).lower():
            continue
        tbl = sec.find("table", class_="b-fight-details__table") or sec.find("table")
        if not tbl:
            continue
        thead = tbl.find("thead")
        if not thead:
            continue
        heads = [th.get_text(strip=True) for th in thead.find_all("th")]
        if "Head" in heads and "Distance" in heads and "Clinch" in heads and "Ground" in heads:
            body = tbl.find("tbody")
            if body and body.find_all("tr"):
                return tbl
    return None


def _two_fighter_cell_ps(row, td_index: int) -> tuple[str, str] | None:
    tds = row.find_all("td")
    if td_index >= len(tds):
        return None
    ps = tds[td_index].find_all("p")
    if len(ps) < 2:
        return None
    return ps[0].get_text(strip=True), ps[1].get_text(strip=True)


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
        weight_class  = derive_weight_class_from_fighter_page(soup),
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

        # Col 6: event name + date (layout as of 2026; older pages used col 5)
        event_tag  = cols[6].find("a")
        event_name = get_text(event_tag)
        event_url  = event_tag.get("href", "") if event_tag else ""
        event_id   = extract_id_from_url(event_url)

        date_text = extract_event_date_text_from_cell(cols[6])
        event_date_unix = parse_date_to_unix(date_text) if date_text else None

        # Title fight detection: ufcstats embeds a belt img in the event cell
        is_title_fight = 1 if cols[6].find("img") and "belt" in str(cols[6]) else 0

        # Col 7: method (e.g. "KO/TKO  Punches")
        method_lines = [t.strip() for t in cols[7].get_text(separator="\n").split("\n") if t.strip()]
        result_method        = method_lines[0] if method_lines else None
        result_method_detail = method_lines[1] if len(method_lines) > 1 else None

        # Normalise method strings
        if result_method:
            result_method = re.sub(r'\s+', ' ', result_method).strip()

        # Col 8: round
        round_text  = get_text(cols[8])
        result_round = int(round_text) if round_text.isdigit() else None

        # Col 9: time
        time_text = get_text(cols[9])
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
        ))

        if event_id:
            events.append(Event(
                ufc_event_id = event_id,
                name         = event_name,
                event_date   = event_date_unix,
            ))

    # Infer fighter weight class from most common weight class in fights
    # (for now just leave it None; patch in Week 2 when we have full fight data)
    # fighter.weight_class = most_common_weight_class(fights)

    return fighter, fights, events


def scrape_fight_details(ufc_fight_id: str) -> tuple[list[RoundStatEntry], str | None]:
    """
    Fetch fight-details once: parse weight class from the fight head, then round tables.
    Returns (round_stat_entries, weight_class_label or None).
    """
    url = f"{BASE_URL}/fight-details/{ufc_fight_id}"
    soup = get_page(url)
    fight_wc = parse_weight_class_from_fight_soup(soup)

    persons = soup.select("div.b-fight-details__persons a.b-fight-details__person-link")
    if len(persons) < 2:
        return [], fight_wc
    order = [extract_id_from_url(p.get("href", "")) for p in persons[:2]]
    if not all(order):
        return [], fight_wc

    tot_tbl = _per_round_totals_table(soup)
    sig_tbl = _per_round_sig_target_table(soup)
    if not tot_tbl:
        return [], fight_wc

    tot_rows = tot_tbl.select("tbody tr")
    sig_rows = sig_tbl.select("tbody tr") if sig_tbl else []

    out: list[RoundStatEntry] = []
    for r_idx, trow in enumerate(tot_rows):
        round_number = r_idx + 1
        if round_number > 5:
            break
        srow = sig_rows[r_idx] if r_idx < len(sig_rows) else None

        for fi, ufc_fid in enumerate(order):
            kd = parse_int_cell((_two_fighter_cell_ps(trow, 1) or ("0", "0"))[fi])
            sig_txt = (_two_fighter_cell_ps(trow, 2) or ("0 of 0", "0 of 0"))[fi]
            tot_txt = (_two_fighter_cell_ps(trow, 4) or ("0 of 0", "0 of 0"))[fi]
            td_txt = (_two_fighter_cell_ps(trow, 5) or ("0 of 0", "0 of 0"))[fi]
            sub_txt = (_two_fighter_cell_ps(trow, 7) or ("0", "0"))[fi]
            rev_txt = (_two_fighter_cell_ps(trow, 8) or ("0", "0"))[fi]
            ctrl_txt = (_two_fighter_cell_ps(trow, 9) or ("0:00", "0:00"))[fi]

            sig_l, sig_a = parse_landed_attempted(sig_txt)
            tot_l, tot_a = parse_landed_attempted(tot_txt)
            td_l, td_a = parse_landed_attempted(td_txt)

            head = body = leg = dist = clinch = ground = 0
            if srow:
                head = parse_landed_only((_two_fighter_cell_ps(srow, 3) or ("0 of 0", "0 of 0"))[fi])
                body = parse_landed_only((_two_fighter_cell_ps(srow, 4) or ("0 of 0", "0 of 0"))[fi])
                leg = parse_landed_only((_two_fighter_cell_ps(srow, 5) or ("0 of 0", "0 of 0"))[fi])
                dist = parse_landed_only((_two_fighter_cell_ps(srow, 6) or ("0 of 0", "0 of 0"))[fi])
                clinch = parse_landed_only((_two_fighter_cell_ps(srow, 7) or ("0 of 0", "0 of 0"))[fi])
                ground = parse_landed_only((_two_fighter_cell_ps(srow, 8) or ("0 of 0", "0 of 0"))[fi])

            out.append(
                RoundStatEntry(
                    fighter_ufc_id=ufc_fid,
                    round_number=round_number,
                    sig_strikes_landed=sig_l,
                    sig_strikes_attempted=sig_a,
                    total_strikes_landed=tot_l,
                    total_strikes_attempted=tot_a,
                    takedowns_landed=td_l,
                    takedowns_attempted=td_a,
                    sub_attempts=parse_int_cell(sub_txt),
                    reversals=parse_int_cell(rev_txt),
                    knockdowns=kd,
                    control_time_seconds=parse_control_seconds(ctrl_txt),
                    head_strikes_landed=head,
                    body_strikes_landed=body,
                    leg_strikes_landed=leg,
                    distance_strikes_landed=dist,
                    clinch_strikes_landed=clinch,
                    ground_strikes_landed=ground,
                )
            )
    return out, fight_wc


def scrape_fight_round_stats(ufc_fight_id: str) -> list[RoundStatEntry]:
    """Backward-compatible: round rows only."""
    return scrape_fight_details(ufc_fight_id)[0]


def discover_fighter_ids() -> list[str]:
    """
    Collect every fighter id from the official directory (last name A–Z, all pages).
    """
    ids: set[str] = set()
    for char in "abcdefghijklmnopqrstuvwxyz":
        url = f"{BASE_URL}/statistics/fighters?char={char}&page=all"
        soup = get_page(url)
        for a in soup.select('a[href*="fighter-details"]'):
            href = (a.get("href") or "").strip()
            ufc_id = extract_id_from_url(href)
            if ufc_id:
                ids.add(ufc_id)
    return sorted(ids)


def persist_fighter_bundle(conn: sqlite3.Connection, fighter_id: str) -> tuple[Fighter, int]:
    """
    Scrape one fighter, upsert bio, ensure opponent placeholders, insert fights/events.
    One DB transaction per fighter so progress survives interruptions.
    Returns (fighter, number of fights parsed from the profile).
    """
    fighter, fights, events = scrape_fighter(fighter_id)
    with conn:
        main_fighter_db_id = insert_fighter(conn, fighter)
        fighter_id_map: dict[str, int] = {fighter.ufc_id: main_fighter_db_id}

        event_id_map: dict[str, int] = {}
        for event in events:
            if event.ufc_event_id and event.ufc_event_id not in event_id_map:
                event_id_map[event.ufc_event_id] = insert_event(conn, event)

        opponent_ids: set[str] = set()
        for fight in fights:
            for uid in (fight.fighter1_ufc_id, fight.fighter2_ufc_id):
                if uid != fighter_id:
                    opponent_ids.add(uid)

        for opp_id in opponent_ids:
            fighter_id_map[opp_id] = ensure_placeholder_fighter(conn, opp_id)

        for fight in fights:
            insert_fight(conn, fight, fighter_id_map, event_id_map)

    for fight in fights:
        try:
            persist_round_stats_for_fight(conn, fight.ufc_fight_id, fighter_id_map)
        except Exception:
            pass

    conn.commit()
    return fighter, len(fights)


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
            weight_class  = COALESCE(excluded.weight_class, fighters.weight_class),
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
    """Insert or update an event. Returns the row's internal id."""
    event_date = e.event_date if e.event_date is not None else 0
    conn.execute("""
        INSERT INTO events (ufc_event_id, name, event_date)
        VALUES (?,?,?)
        ON CONFLICT(ufc_event_id) DO UPDATE SET
            name = excluded.name,
            event_date = CASE
                WHEN excluded.event_date != 0 THEN excluded.event_date
                ELSE events.event_date
            END
    """, (e.ufc_event_id, e.name, event_date))
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
        print(f"  [skip] fight {fight.ufc_fight_id} - missing FK reference")
        return

    conn.execute("""
        INSERT INTO fights
            (ufc_fight_id, fighter1_id, fighter2_id, event_id, winner_id,
             result_method, result_method_detail, result_round,
             result_time_seconds, weight_class, is_title_fight)
        VALUES (?,?,?,?,?,?,?,?,?,?,?)
        ON CONFLICT(ufc_fight_id) DO NOTHING
    """, (
        fight.ufc_fight_id, f1_id, f2_id, event_id, winner_id,
        fight.result_method, fight.result_method_detail, fight.result_round,
        fight.result_time_seconds, fight.weight_class,
        fight.is_title_fight,
    ))


def get_fight_db_id(conn: sqlite3.Connection, ufc_fight_id: str) -> int | None:
    row = conn.execute("SELECT id FROM fights WHERE ufc_fight_id = ?", (ufc_fight_id,)).fetchone()
    return int(row["id"]) if row else None


def fight_already_has_round_stats(conn: sqlite3.Connection, fight_db_id: int) -> bool:
    """True if we already stored any round row for this fight (skip re-fetching fight-details)."""
    row = conn.execute(
        "SELECT 1 FROM round_stats WHERE fight_id = ? LIMIT 1",
        (fight_db_id,),
    ).fetchone()
    return row is not None


def fight_needs_fight_detail_fetch(conn: sqlite3.Connection, fight_db_id: int) -> bool:
    """Fetch fight-details if we lack round stats and/or fight weight_class."""
    if not fight_already_has_round_stats(conn, fight_db_id):
        return True
    row = conn.execute(
        "SELECT COALESCE(TRIM(weight_class), '') AS w FROM fights WHERE id = ?",
        (fight_db_id,),
    ).fetchone()
    return not row or row["w"] == ""


def upsert_round_stats_batch(
    conn: sqlite3.Connection,
    fight_db_id: int,
    ufc_to_id: dict[str, int],
    entries: list[RoundStatEntry],
) -> None:
    sql = """
    INSERT INTO round_stats (
        fight_id, fighter_id, round_number,
        sig_strikes_landed, sig_strikes_attempted,
        total_strikes_landed, total_strikes_attempted,
        takedowns_landed, takedowns_attempted,
        sub_attempts, reversals, knockdowns, control_time_seconds,
        head_strikes_landed, body_strikes_landed, leg_strikes_landed,
        distance_strikes_landed, clinch_strikes_landed, ground_strikes_landed
    ) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
    ON CONFLICT(fight_id, fighter_id, round_number) DO UPDATE SET
        sig_strikes_landed = excluded.sig_strikes_landed,
        sig_strikes_attempted = excluded.sig_strikes_attempted,
        total_strikes_landed = excluded.total_strikes_landed,
        total_strikes_attempted = excluded.total_strikes_attempted,
        takedowns_landed = excluded.takedowns_landed,
        takedowns_attempted = excluded.takedowns_attempted,
        sub_attempts = excluded.sub_attempts,
        reversals = excluded.reversals,
        knockdowns = excluded.knockdowns,
        control_time_seconds = excluded.control_time_seconds,
        head_strikes_landed = excluded.head_strikes_landed,
        body_strikes_landed = excluded.body_strikes_landed,
        leg_strikes_landed = excluded.leg_strikes_landed,
        distance_strikes_landed = excluded.distance_strikes_landed,
        clinch_strikes_landed = excluded.clinch_strikes_landed,
        ground_strikes_landed = excluded.ground_strikes_landed
    """
    for e in entries:
        fid = ufc_to_id.get(e.fighter_ufc_id)
        if not fid:
            continue
        conn.execute(
            sql,
            (
                fight_db_id,
                fid,
                e.round_number,
                e.sig_strikes_landed,
                e.sig_strikes_attempted,
                e.total_strikes_landed,
                e.total_strikes_attempted,
                e.takedowns_landed,
                e.takedowns_attempted,
                e.sub_attempts,
                e.reversals,
                e.knockdowns,
                e.control_time_seconds,
                e.head_strikes_landed,
                e.body_strikes_landed,
                e.leg_strikes_landed,
                e.distance_strikes_landed,
                e.clinch_strikes_landed,
                e.ground_strikes_landed,
            ),
        )


def persist_round_stats_for_fight(
    conn: sqlite3.Connection,
    ufc_fight_id: str,
    ufc_to_id: dict[str, int],
) -> None:
    db_fid = get_fight_db_id(conn, ufc_fight_id)
    if not db_fid:
        return
    if not fight_needs_fight_detail_fetch(conn, db_fid):
        return
    try:
        entries, fight_wc = scrape_fight_details(ufc_fight_id)
    except Exception:
        return
    if entries:
        upsert_round_stats_batch(conn, db_fid, ufc_to_id, entries)
    if fight_wc:
        conn.execute(
            "UPDATE fights SET weight_class = ? WHERE id = ?",
            (fight_wc, db_fid),
        )


def normalize_fighter_name(name: str) -> str:
    return re.sub(r"\s+", " ", (name or "").strip().lower())


def resolve_fighter_id_by_name(conn: sqlite3.Connection, name: str) -> int | None:
    key = normalize_fighter_name(name)
    if not key:
        return None
    row = conn.execute(
        "SELECT id FROM fighters WHERE lower(trim(name)) = ?",
        (key,),
    ).fetchone()
    if row:
        return int(row["id"])
    row = conn.execute(
        "SELECT id FROM fighters WHERE replace(lower(trim(name)), '''', '') = ?",
        (key.replace("'", ""),),
    ).fetchone()
    if row:
        return int(row["id"])
    return None


def scrape_ufc_rankings_rows() -> list[tuple[str, int, str]]:
    """Parse UFC_RANKINGS_URL - returns (weight_class, rank, display_name)."""
    r = requests.get(UFC_RANKINGS_URL, headers=HEADERS, timeout=25)
    r.raise_for_status()
    time.sleep(DELAY)
    soup = BeautifulSoup(r.text, "html.parser")
    out: list[tuple[str, int, str]] = []
    for grp in soup.select(".view-grouping"):
        hdr = grp.select_one(".view-grouping-header")
        if not hdr:
            continue
        weight_class = re.sub(r"Top Rank.*$", "", hdr.get_text(strip=True), flags=re.I).strip()
        if not weight_class:
            continue

        tbody = grp.find("tbody")
        body_first_name: str | None = None
        if tbody:
            tr0 = tbody.find("tr")
            if tr0:
                a0 = tr0.select_one(".views-field-title a")
                if a0:
                    body_first_name = a0.get_text(strip=True)

        cap = grp.select_one(".rankings--athlete--champion")
        champ_a = cap.select_one("h5 a") if cap else None
        if champ_a:
            cname = champ_a.get_text(strip=True)
            dup = (
                body_first_name is not None
                and cname.strip().lower() == body_first_name.strip().lower()
            )
            if not dup:
                out.append((weight_class, 0, cname))

        if tbody:
            for tr in tbody.find_all("tr"):
                rank_td = tr.select_one(".views-field-weight-class-rank")
                title_a = tr.select_one(".views-field-title a")
                if not rank_td or not title_a:
                    continue
                rk_txt = rank_td.get_text(strip=True)
                if not rk_txt.isdigit():
                    continue
                out.append((weight_class, int(rk_txt), title_a.get_text(strip=True)))
    return out


def sync_fighter_rankings(conn: sqlite3.Connection) -> tuple[int, int]:
    """Replace fighter_rankings from ufc.com. Returns (inserted, skipped_unmatched)."""
    conn.execute("DELETE FROM fighter_rankings")
    rows = scrape_ufc_rankings_rows()
    now = int(datetime.now().timestamp())
    inserted = 0
    skipped = 0
    for wc, rk, name in rows:
        fid = resolve_fighter_id_by_name(conn, name)
        if not fid:
            skipped += 1
            continue
        conn.execute(
            """
            INSERT INTO fighter_rankings (fighter_id, weight_class, rank, updated_at)
            VALUES (?,?,?,?)
            """,
            (fid, wc, rk, now),
        )
        inserted += 1
    conn.commit()
    return inserted, skipped


# ─────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────

def main() -> None:
    global DELAY
    default_delay = DELAY
    parser = argparse.ArgumentParser(
        description="Scrape ufcstats.com into SQLite (fighters, fights, round stats; UFC.com rankings).",
    )
    parser.add_argument(
        "fighter_id",
        nargs="?",
        metavar="ID",
        help="Single 16-char fighter id. If omitted, scrapes the full fighter directory (every fighter, every listed fight).",
    )
    parser.add_argument(
        "--delay",
        type=float,
        default=default_delay,
        metavar="SEC",
        help=f"Seconds to sleep after each HTTP request (default: {default_delay}).",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=None,
        metavar="N",
        help="With no fighter id: only scrape the first N fighters from the A-Z directory (quick test).",
    )
    args = parser.parse_args()

    if args.fighter_id and args.limit is not None:
        parser.error("Use either a fighter id or --limit, not both.")

    DELAY = max(0.0, args.delay)

    conn = get_connection(DB_PATH)

    try:
        if args.fighter_id:
            fighter_id = args.fighter_id.strip().lower()
            print(f"Scraping fighter: {fighter_id}")
            fighter, n_fights = persist_fighter_bundle(conn, fighter_id)
            print(f"  Saved: {fighter.name} - {n_fights} fights from profile")

            print()
            print("-" * 50)
            print(f"  {fighter.name}")
            print("-" * 50)
            print(f"  Stance:    {fighter.stance or 'N/A'}")
            print(f"  Height:    {fighter.height_cm} cm" if fighter.height_cm else "  Height:    N/A")
            print(f"  Reach:     {fighter.reach_cm} cm" if fighter.reach_cm else "  Reach:     N/A")
            dob = (
                datetime.fromtimestamp(fighter.date_of_birth).strftime("%b %d, %Y")
                if fighter.date_of_birth
                else "N/A"
            )
            print(f"  DOB:       {dob}")
            print(f"  Fights:    {n_fights} scraped")
            print(f"  Saved to:  {DB_PATH}")
            print("-" * 50)

        else:
            print("Discovering fighters (A-Z directory, page=all) ...")
            ids = discover_fighter_ids()
            if args.limit is not None:
                if args.limit < 1:
                    parser.error("--limit must be at least 1")
                ids = ids[: args.limit]
                print(f"  (--limit) scraping first {len(ids)} fighters only (test / partial run).")
            est_min = len(ids) * DELAY / 60.0
            print(f"  {len(ids)} fighters in this run (rough lower bound ~{est_min:.1f} min from delay alone; add time per fight-detail).")
            print("  Scraping each profile; fights dedupe via ON CONFLICT.")
            print()

            errors = 0
            for i, fighter_id in enumerate(ids, start=1):
                try:
                    fighter, _n = persist_fighter_bundle(conn, fighter_id)
                    if i == 1 or i % 25 == 0 or i == len(ids):
                        print(f"  [{i}/{len(ids)}] {fighter.name} ({fighter_id})")
                except KeyboardInterrupt:
                    print(f"\nStopped at [{i}/{len(ids)}]. Database has partial progress.")
                    raise
                except Exception as exc:
                    errors += 1
                    print(f"  [error] {fighter_id}: {exc}")

            print()
            print(f"Done. Processed {len(ids)} fighters with {errors} errors. Database: {DB_PATH}")

        try:
            ins, sk = sync_fighter_rankings(conn)
            print(f"  UFC.com rankings: {ins} rows stored, {sk} names not matched in local fighters table.")
        except Exception as exc:
            print(f"  [warn] rankings sync failed: {exc}")

    finally:
        conn.close()


if __name__ == "__main__":
    main()
