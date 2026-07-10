"""
UFC Stats Scraper - fighter pages, fight details, and SQLite

Writes fighters, events, fights, round_stats (from ufcstats fight pages once per fight), and
fighter_rankings (from ufc.com meta rankings - name-matched to DB).

Usage:
    python scraper.py                    # full catalog: all fighters + all fights (slow)
    python scraper.py 0d8011111be000b2   # single fighter by ufcstats id
    python scraper.py --sync-recent      # fighters on completed cards in the last 14 days
    python scraper.py --sync-recent --since-days 7
    python scraper.py --rankings-only    # UFC.com meta rankings only (no ufcstats scraping)
    python scraper.py --limit 12 --delay 0.5   # quick test: first 12 fighters in directory only
    python scraper.py --delay 2.0        # be kinder to the server between requests
"""

import argparse
import hashlib
import os
import sqlite3
import re
import sys
import time
import unicodedata
from datetime import datetime, timedelta, timezone
from dataclasses import dataclass
from pathlib import Path

import requests
from bs4 import BeautifulSoup


# ─────────────────────────────────────────────────────────────
# Config
# ─────────────────────────────────────────────────────────────

BASE_URL    = "http://ufcstats.com"
EVENTS_COMPLETED_URL = f"{BASE_URL}/statistics/events/completed?page=all"
EVENTS_UPCOMING_URL = f"{BASE_URL}/statistics/events/upcoming?page=all"
DB_PATH     = "ufc.db"
HEADERS     = {
    "User-Agent": (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
    ),
}
DELAY       = 1.5  # seconds between requests — be polite to the server

_ufcstats_session: requests.Session | None = None


def _get_ufcstats_session() -> requests.Session:
    global _ufcstats_session
    if _ufcstats_session is None:
        _ufcstats_session = requests.Session()
    return _ufcstats_session


def _is_ufcstats_challenge(html: str) -> bool:
    return "Checking your browser" in html or 'var nonce="' in html


def _extract_ufcstats_challenge(html: str) -> tuple[str, int] | None:
    nonce_match = re.search(r'var nonce="([^"]+)"', html)
    if not nonce_match:
        return None
    zeros_match = re.search(r"new Array\((\d+)\+1\)\.join\('0'\)", html)
    zeros = int(zeros_match.group(1)) if zeros_match else 2
    return nonce_match.group(1), zeros


def _solve_ufcstats_pow(nonce: str, zeros: int) -> int:
    target = "0" * zeros
    n = 0
    while True:
        digest = hashlib.sha256(f"{nonce}:{n}".encode()).hexdigest()
        if digest.startswith(target):
            return n
        n += 1


def _fetch_ufcstats_html(url: str) -> str:
    """GET a ufcstats.com page, solving the JS proof-of-work gate when present."""
    session = _get_ufcstats_session()
    response = session.get(url, headers=HEADERS, timeout=15)
    response.raise_for_status()
    if not _is_ufcstats_challenge(response.text):
        return response.text

    challenge = _extract_ufcstats_challenge(response.text)
    if challenge is None:
        raise RuntimeError(f"ufcstats anti-bot page at {url} but could not parse challenge")
    nonce, zeros = challenge
    solution = _solve_ufcstats_pow(nonce, zeros)
    post = session.post(
        f"{BASE_URL}/__c",
        data={"nonce": nonce, "n": str(solution)},
        headers={**HEADERS, "Content-Type": "application/x-www-form-urlencoded"},
        timeout=15,
    )
    post.raise_for_status()

    retry = session.get(url, headers=HEADERS, timeout=15)
    retry.raise_for_status()
    if _is_ufcstats_challenge(retry.text):
        raise RuntimeError(f"ufcstats anti-bot challenge still active after solve ({url})")
    return retry.text


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
class EventFight:
    fighter1_name:    str
    fighter2_name:    str
    fighter1_ufc_id:  str
    fighter2_ufc_id:  str
    weight_class:     str | None


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


# UFC.com hosts division meta rankings; ufcstats.com does not.
UFC_RANKINGS_URL = "https://www.ufc.com/rankings"

def get_page(url: str) -> BeautifulSoup:
    """Fetch a page and return a BeautifulSoup object."""
    html = _fetch_ufcstats_html(url)
    time.sleep(DELAY)
    return BeautifulSoup(html, "html.parser")


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
            dt = datetime.strptime(text, fmt)
            # UTC avoids Windows OSError(22) from naive datetime.timestamp() + localtime.
            return int(dt.replace(tzinfo=timezone.utc).timestamp())
        except ValueError:
            continue
    return None


def format_unix_date(unix_ts: int) -> str:
    """Format Unix seconds as a calendar date (Windows-safe for pre-1970 DOBs)."""
    dt = datetime(1970, 1, 1, tzinfo=timezone.utc) + timedelta(seconds=unix_ts)
    return dt.strftime("%b %d, %Y")


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
    """Heuristic: women's divisions often appear in page copy.

    Note: this should not affect men's lbs→division mapping; it only decides whether
    to use women's vs men's division labels when interpreting the *same* lbs value.
    """
    sample = soup.get_text(separator=" ", strip=False)[:25000]
    # UFCStats pages are inconsistent: sometimes the division string is present
    # ("Women's Strawweight"), sometimes only "Women's" appears elsewhere on the page.
    return bool(
        re.search(
            r"Women'?s(?:\s+(?:Strawweight|Flyweight|Bantamweight|Featherweight))?\b",
            sample,
            re.I,
        )
    )

def _weight_class_is_womens(label: str | None) -> bool:
    """True if a weight class label denotes a women's division."""
    if not label:
        return False
    return bool(re.match(r"^\s*Women'?s\b", label, re.I))


def _weight_class_is_catchweight(label: str | None) -> bool:
    """True if a fight was booked at a catchweight (e.g. 'Catchweight', 'Catch Weight')."""
    if not label:
        return False
    return bool(re.search(r"catch\s*weight", label, re.I))


def _fights_newest_first(
    fights: list[FightRow],
    fight_dates: dict[str, int | None],
) -> list[str]:
    """Fight ids in reverse chronological order (profile row order breaks date ties)."""
    return [
        fight.ufc_fight_id
        for _, fight in sorted(
            enumerate(fights),
            key=lambda t: (
                fight_dates.get(t[1].ufc_fight_id) is None,
                -(fight_dates.get(t[1].ufc_fight_id) or 0),
                t[0],
            ),
        )
    ]


def _infer_womens_from_fight_history(
    fights: list[FightRow],
    fight_dates: dict[str, int | None],
) -> bool:
    """Use the most recent non-catchweight fight to decide women's vs men's divisions."""
    for fight_id in _fights_newest_first(fights, fight_dates):
        try:
            _entries, wc = scrape_fight_details(fight_id)
        except Exception:
            continue
        if not wc or _weight_class_is_catchweight(wc):
            continue
        return _weight_class_is_womens(wc)
    return False


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

    # ── Fight history ────────────────────────────────────────

    fights: list[FightRow] = []
    events: list[Event]    = []
    fight_dates: dict[str, int | None] = {}  # ufc_fight_id -> event_date_unix

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
        fight_dates[fight_id] = event_date_unix

        if event_id:
            events.append(Event(
                ufc_event_id = event_id,
                name         = event_name,
                event_date   = event_date_unix,
            ))

    # Determine whether this fighter is in a women's division from fight history.
    # Use the most recent non-catchweight fight; skip catchweights and walk further back.
    # If there are no fights, or no usable fight weight class is found, womens=False.
    womens = _infer_womens_from_fight_history(fights, fight_dates) if fights else False

    # Build fighter record after we know whether they're in women's divisions.
    lbs = parse_fighter_weight_lbs_from_soup(soup)
    fighter = Fighter(
        ufc_id        = fighter_id,
        name          = name,
        stance        = stance if stance and stance != "--" else None,
        reach_cm      = inches_to_cm(reach_raw) if reach_raw and reach_raw != "--" else None,
        height_cm     = feet_inches_to_cm(height_raw) if height_raw else None,
        weight_class  = lbs_to_fighter_weight_class(lbs, womens) if lbs is not None else None,
        date_of_birth = parse_date_to_unix(dob_raw) if dob_raw and dob_raw != "--" else None,
        profile_url   = url,
        last_updated  = int(datetime.now().timestamp()),
    )

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


def utc_day_start(dt: datetime | None = None) -> int:
    """Unix timestamp for 00:00:00 UTC on the given day (default: today)."""
    dt = dt or datetime.now(timezone.utc)
    start = datetime(dt.year, dt.month, dt.day, tzinfo=timezone.utc)
    return int(start.timestamp())


def discover_completed_events() -> list[Event]:
    """All events listed on ufcstats completed-events index (includes future dates)."""
    soup = get_page(EVENTS_COMPLETED_URL)
    return _parse_events_index(soup)


def discover_upcoming_events() -> list[Event]:
    """Upcoming events listed on ufcstats upcoming-events index."""
    soup = get_page(EVENTS_UPCOMING_URL)
    return _parse_events_index(soup)


def _parse_events_index(soup: BeautifulSoup) -> list[Event]:
    out: list[Event] = []
    for row in soup.select("tr.b-statistics__table-row"):
        link = row.select_one('a[href*="event-details"]')
        if not link:
            continue
        event_id = extract_id_from_url(link.get("href", "") or "")
        if not event_id:
            continue
        date_span = row.select_one("span.b-statistics__date")
        date_text = get_text(date_span)
        out.append(Event(
            ufc_event_id=event_id,
            name=get_text(link),
            event_date=parse_date_to_unix(date_text) if date_text else None,
        ))
    return out


def select_next_upcoming_event(events: list[Event]) -> Event | None:
    """Nearest upcoming event by date (UTC day start), or None."""
    today_start = utc_day_start()
    upcoming = [e for e in events if e.event_date is not None and e.event_date >= today_start]
    if not upcoming:
        return None
    upcoming.sort(key=lambda e: e.event_date or 0)
    return upcoming[0]


def scrape_event_fights(event_id: str) -> list[EventFight]:
    """Fight pairings from an ufcstats event-details page (main card first)."""
    url = f"{BASE_URL}/event-details/{event_id}"
    soup = get_page(url)
    fights: list[EventFight] = []
    for row in soup.select("tr.b-fight-details__table-row"):
        links = row.select('a[href*="fighter-details"]')
        if len(links) < 2:
            continue
        f1_id = extract_id_from_url(links[0].get("href", "") or "")
        f2_id = extract_id_from_url(links[1].get("href", "") or "")
        if not f1_id or not f2_id:
            continue
        cols = row.find_all("td")
        weight_class = get_text(cols[6]) if len(cols) > 6 else None
        fights.append(EventFight(
            fighter1_name=get_text(links[0]),
            fighter2_name=get_text(links[1]),
            fighter1_ufc_id=f1_id,
            fighter2_ufc_id=f2_id,
            weight_class=weight_class or None,
        ))
    return fights


def scrape_event_meta(event_id: str) -> Event:
    """Event name and date from an ufcstats event-details page."""
    url = f"{BASE_URL}/event-details/{event_id}"
    soup = get_page(url)
    name = get_text(soup.find("h2")) or f"Event {event_id}"
    event_date: int | None = None
    for item in soup.select("li.b-list__box-list-item"):
        text = get_text(item)
        if text.lower().startswith("date:"):
            date_text = text.split(":", 1)[1].strip()
            event_date = parse_date_to_unix(date_text) if date_text else None
            break
    return Event(
        ufc_event_id=event_id.lower(),
        name=name,
        event_date=event_date,
    )


def find_event_by_id(event_id: str) -> Event | None:
    """Look up an event on ufcstats upcoming or completed indexes."""
    event_id = event_id.lower()
    for discover in (discover_upcoming_events, discover_completed_events):
        for event in discover():
            if event.ufc_event_id == event_id:
                return event
    return None


def select_events_for_sync(events: list[Event], since_days: int) -> list[Event]:
    """Completed events with event_date in [today - since_days, today] (UTC)."""
    today_start = utc_day_start()
    cutoff = today_start - since_days * 86400
    selected: list[Event] = []
    for event in events:
        if event.event_date is None:
            continue
        if event.event_date > today_start:
            continue
        if event.event_date < cutoff:
            continue
        selected.append(event)
    selected.sort(key=lambda e: e.event_date or 0)
    return selected


def scrape_event_fighter_ids(event_id: str) -> list[str]:
    """Fighter ufcstats ids appearing on an event-details page."""
    url = f"{BASE_URL}/event-details/{event_id}"
    soup = get_page(url)
    ids: set[str] = set()
    for link in soup.select('a[href*="fighter-details"]'):
        fighter_id = extract_id_from_url(link.get("href", "") or "")
        if fighter_id:
            ids.add(fighter_id)
    return sorted(ids)


def sync_recent_events(conn: sqlite3.Connection, since_days: int = 14) -> None:
    """
    Refresh fighters who competed on completed cards in the last since_days (UTC).
    Idempotent: safe to run on a schedule after each UFC event.
    """
    print(f"Incremental sync: completed events in the last {since_days} day(s) ...")
    events = select_events_for_sync(discover_completed_events(), since_days)
    if not events:
        print("  No completed events in that window.")
        return

    print(f"  {len(events)} event(s):")
    for event in events:
        when = format_unix_date(event.event_date) if event.event_date else "?"
        print(f"    - {event.name} ({when})")

    fighter_ids: set[str] = set()
    for event in events:
        for fighter_id in scrape_event_fighter_ids(event.ufc_event_id):
            fighter_ids.add(fighter_id)

    if not fighter_ids:
        print("  No fighters found on those event pages.")
        return

    print(f"  Scraping {len(fighter_ids)} unique fighter profile(s) ...")
    errors = 0
    fighters_to_refresh: set[int] = set()
    for i, fighter_id in enumerate(sorted(fighter_ids), start=1):
        try:
            fighter, _n, refreshed = persist_fighter_bundle(
                conn, fighter_id, refresh_archetypes=False,
            )
            fighters_to_refresh.update(refreshed)
            if i == 1 or i % 10 == 0 or i == len(fighter_ids):
                print(f"  [{i}/{len(fighter_ids)}] {fighter.name} ({fighter_id})")
        except KeyboardInterrupt:
            print(f"\nStopped at [{i}/{len(fighter_ids)}]. Database has partial progress.")
            raise
        except Exception as exc:
            errors += 1
            print(f"  [error] {fighter_id}: {exc}")

    if fighters_to_refresh:
        refresh_fighter_archetypes(conn, fighters_to_refresh)
        refresh_fighter_momentum(conn, fighters_to_refresh)
        conn.commit()
        print(f"  Recalculated archetypes and momentum for {len(fighters_to_refresh)} fighter(s) with new fight data.")
    else:
        print("  No new fight data; archetypes unchanged.")

    print(f"  Done. {len(fighter_ids)} fighters with {errors} error(s).")


_archetype_store = None
_archetype_unavailable = False


def _resolve_ufc_db_library() -> Path:
    """Locate the built ufc_db shared library."""
    root = Path(__file__).resolve().parent
    name = "ufc_db.dll" if sys.platform == "win32" else (
        "libufc_db.dylib" if sys.platform == "darwin" else "libufc_db.so"
    )
    for path in (
        root / "build" / name,
        root / "build" / "Release" / name,
        root / "build" / "Debug" / name,
    ):
        if path.is_file():
            return path
    return root / "build" / "Release" / name


class _UfcDbStore:
    """Minimal ctypes wrapper for scraper score refresh (ufc_db.dll)."""

    def __init__(self, db_path: Path, lib_path: Path) -> None:
        from ctypes import CDLL, POINTER, byref, c_char_p, c_double, c_int, c_longlong, c_void_p

        if not lib_path.is_file():
            raise FileNotFoundError(
                f"ufc_db shared library not found at {lib_path}. "
                "Build the C++ project first (cmake --build build --config Release)."
            )
        if sys.platform == "win32" and hasattr(os, "add_dll_directory"):
            os.add_dll_directory(str(lib_path.parent))

        self._lib = CDLL(str(lib_path))
        self._lib.ufc_db_open.restype = c_void_p
        self._lib.ufc_db_open.argtypes = [c_char_p]
        self._lib.ufc_db_close.argtypes = [c_void_p]
        self._lib.ufc_free_string.argtypes = [c_void_p]
        self._lib.ufc_last_error.restype = c_char_p
        self._lib.ufc_classify_archetype_by_fighter_id.restype = c_void_p
        self._lib.ufc_classify_archetype_by_fighter_id.argtypes = [c_void_p, c_longlong]
        self._lib.ufc_compute_momentum_by_fighter_id_out.argtypes = [
            c_void_p, c_longlong, POINTER(c_double),
        ]
        self._lib.ufc_compute_momentum_by_fighter_id_out.restype = c_int
        self._lib.ufc_compute_resume_by_fighter_id_out.argtypes = [
            c_void_p, c_longlong, POINTER(c_double),
        ]
        self._lib.ufc_compute_resume_by_fighter_id_out.restype = c_int

        self._handle = self._lib.ufc_db_open(str(db_path).encode("utf-8"))
        if not self._handle:
            err = self._lib.ufc_last_error()
            raise RuntimeError(err.decode("utf-8") if err else "ufc_db_open failed")

    def classify_archetype_by_fighter_id(self, fighter_id: int) -> str | None:
        from ctypes import string_at

        ptr = self._lib.ufc_classify_archetype_by_fighter_id(self._handle, fighter_id)
        if not ptr:
            return None
        try:
            return string_at(ptr).decode("utf-8")
        finally:
            self._lib.ufc_free_string(ptr)

    def compute_momentum_by_fighter_id(self, fighter_id: int) -> float | None:
        from ctypes import byref, c_double

        score = c_double()
        ok = self._lib.ufc_compute_momentum_by_fighter_id_out(
            self._handle, fighter_id, byref(score)
        )
        return float(score.value) if ok else None

    def compute_resume_by_fighter_id(self, fighter_id: int) -> float:
        from ctypes import byref, c_double

        score = c_double()
        ok = self._lib.ufc_compute_resume_by_fighter_id_out(
            self._handle, fighter_id, byref(score)
        )
        if not ok:
            err = self._lib.ufc_last_error()
            raise RuntimeError(err.decode("utf-8") if err else "resume score failed")
        return float(score.value)

    def close(self) -> None:
        if self._handle:
            self._lib.ufc_db_close(self._handle)
            self._handle = None


def _get_archetype_store():
    """Lazy-open ufc_db shared library for archetype classification."""
    global _archetype_store, _archetype_unavailable
    if _archetype_unavailable:
        return None
    if _archetype_store is not None:
        return _archetype_store
    try:
        store = _UfcDbStore(DB_PATH, _resolve_ufc_db_library())
        _archetype_store = store
        return store
    except (FileNotFoundError, RuntimeError) as exc:
        _archetype_unavailable = True
        print(f"  [warn] archetype refresh unavailable: {exc}")
        return None


def close_archetype_store() -> None:
    global _archetype_store
    if _archetype_store is not None:
        _archetype_store.close()
        _archetype_store = None


def refresh_fighter_archetypes(conn: sqlite3.Connection, fighter_db_ids: set[int]) -> None:
    """Recompute and persist archetype labels for the given internal fighter ids."""
    if not fighter_db_ids:
        return
    store = _get_archetype_store()
    if not store:
        return
    for fighter_id in fighter_db_ids:
        label = store.classify_archetype_by_fighter_id(fighter_id)
        if label:
            conn.execute(
                "UPDATE fighters SET archetype = ? WHERE id = ?",
                (label, fighter_id),
            )


def refresh_fighter_momentum(conn: sqlite3.Connection, fighter_db_ids: set[int]) -> None:
    """Recompute and persist momentum scores for the given internal fighter ids."""
    if not fighter_db_ids:
        return
    store = _get_archetype_store()
    if not store:
        return
    for fighter_id in fighter_db_ids:
        score = store.compute_momentum_by_fighter_id(fighter_id)
        if score is None:
            conn.execute(
                "UPDATE fighters SET momentum_score = NULL WHERE id = ?",
                (fighter_id,),
            )
        else:
            conn.execute(
                "UPDATE fighters SET momentum_score = ? WHERE id = ?",
                (score, fighter_id),
            )


def ensure_resume_score_column(conn: sqlite3.Connection) -> None:
    columns = {row[1] for row in conn.execute("PRAGMA table_info(fighters)")}
    if "resume_score" not in columns:
        conn.execute("ALTER TABLE fighters ADD COLUMN resume_score REAL")


def refresh_fighter_resume(conn: sqlite3.Connection, fighter_db_ids: set[int]) -> None:
    """Recompute and persist resume scores for the given internal fighter ids."""
    if not fighter_db_ids:
        return
    store = _get_archetype_store()
    if not store:
        return
    ensure_resume_score_column(conn)
    for fighter_id in fighter_db_ids:
        score = store.compute_resume_by_fighter_id(fighter_id)
        conn.execute(
            "UPDATE fighters SET resume_score = ? WHERE id = ?",
            (score, fighter_id),
        )


def refresh_all_fighter_resume(conn: sqlite3.Connection) -> None:
    """Recompute and persist resume scores for all fighters.

    Resume uses current opponent rankings, so it should be refreshed whenever
    fighter_rankings is updated or new fight results are ingested.
    """
    fighter_ids = {int(row[0]) for row in conn.execute("SELECT id FROM fighters")}
    refresh_fighter_resume(conn, fighter_ids)


def refresh_all_fighter_momentum(conn: sqlite3.Connection) -> None:
    """Recompute and persist momentum scores for all fighters.

    Momentum uses opponent rankings, so it should be refreshed whenever
    fighter_rankings is updated.
    """
    fighter_ids = {int(row[0]) for row in conn.execute("SELECT id FROM fighters")}
    refresh_fighter_momentum(conn, fighter_ids)


def persist_fighter_bundle(
    conn: sqlite3.Connection,
    fighter_id: str,
    *,
    refresh_archetypes: bool = True,
) -> tuple[Fighter, int, set[int]]:
    """
    Scrape one fighter, upsert bio, ensure opponent placeholders, insert fights/events.
    One DB transaction per fighter so progress survives interruptions.
    Returns (fighter, number of fights parsed from the profile, internal fighter ids
    whose archetype should be refreshed due to new fights or new round stats).
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

        new_fight_ids: set[str] = set()
        for fight in fights:
            if insert_fight(conn, fight, fighter_id_map, event_id_map):
                new_fight_ids.add(fight.ufc_fight_id)

    fighters_to_refresh: set[int] = set()
    for fight in fights:
        try:
            stats_written, participant_ids = persist_round_stats_for_fight(
                conn, fight.ufc_fight_id, fighter_id_map,
            )
            if stats_written or fight.ufc_fight_id in new_fight_ids:
                fighters_to_refresh.update(participant_ids)
        except Exception:
            if fight.ufc_fight_id in new_fight_ids:
                db_fid = get_fight_db_id(conn, fight.ufc_fight_id)
                if db_fid:
                    fighters_to_refresh.update(get_fight_participant_db_ids(conn, db_fid))

    if refresh_archetypes:
        refresh_fighter_archetypes(conn, fighters_to_refresh)
        refresh_fighter_momentum(conn, fighters_to_refresh)
        refresh_fighter_resume(conn, fighters_to_refresh)

    conn.commit()
    return fighter, len(fights), fighters_to_refresh


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
             weight_class, date_of_birth, profile_url, last_updated)
        VALUES (?,?,?,?,?,?,?,?,?)
        ON CONFLICT(ufc_id) DO UPDATE SET
            name          = excluded.name,
            stance        = excluded.stance,
            reach_cm      = excluded.reach_cm,
            height_cm     = excluded.height_cm,
            weight_class  = COALESCE(excluded.weight_class, fighters.weight_class),
            date_of_birth = excluded.date_of_birth,
            profile_url   = excluded.profile_url,
            last_updated  = excluded.last_updated
    """, (
        f.ufc_id, f.name, f.stance,
        f.reach_cm, f.height_cm, f.weight_class, f.date_of_birth,
        f.profile_url, f.last_updated,
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
                 fighter_id_map: dict[str, int], event_id_map: dict[str, int]) -> bool:
    """Insert or ignore a fight row. Returns True when a new fight row was inserted."""
    f1_id     = fighter_id_map.get(fight.fighter1_ufc_id)
    f2_id     = fighter_id_map.get(fight.fighter2_ufc_id)
    event_id  = event_id_map.get(fight.ufc_event_id)
    winner_id = fighter_id_map.get(fight.winner_ufc_id) if fight.winner_ufc_id else None

    if not f1_id or not f2_id or not event_id:
        print(f"  [skip] fight {fight.ufc_fight_id} - missing FK reference")
        return False

    cur = conn.execute("""
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
    return cur.rowcount > 0


def get_fight_participant_db_ids(conn: sqlite3.Connection, fight_db_id: int) -> list[int]:
    row = conn.execute(
        "SELECT fighter1_id, fighter2_id FROM fights WHERE id = ?",
        (fight_db_id,),
    ).fetchone()
    if not row:
        return []
    return [int(row["fighter1_id"]), int(row["fighter2_id"])]


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
) -> tuple[bool, list[int]]:
    db_fid = get_fight_db_id(conn, ufc_fight_id)
    if not db_fid:
        return False, []
    participants = get_fight_participant_db_ids(conn, db_fid)
    if not fight_needs_fight_detail_fetch(conn, db_fid):
        return False, participants
    try:
        entries, fight_wc = scrape_fight_details(ufc_fight_id)
    except Exception:
        return False, participants
    stats_written = bool(entries)
    if entries:
        upsert_round_stats_batch(conn, db_fid, ufc_to_id, entries)
    if fight_wc:
        conn.execute(
            "UPDATE fights SET weight_class = ? WHERE id = ?",
            (fight_wc, db_fid),
        )
    return stats_written, participants


_APOSTROPHE_LIKE = "'\u2018\u2019\u201a\u201b\u2032\u02bc\u0060"

# Letters that NFKD does not fold to ASCII (Polish ł, etc.) — map before stripping punctuation.
_LATIN_TRANSLIT = str.maketrans(
    {
        "ł": "l",
        "ß": "ss",
        "ø": "o",
        "đ": "d",
        "æ": "ae",
        "œ": "oe",
    }
)


def normalize_fighter_name(name: str) -> str:
    """
    Canonical key for matching ufc.com rankings to ufcstats names.
    Folds accents (Benoît -> benoit), strips apostrophe variants (Lone'er -> loneer),
    transliterates ł -> l (Jan Błachowicz -> jan blachowicz).
    """
    if not name:
        return ""
    s = name.strip().lower().translate(_LATIN_TRANSLIT)
    s = unicodedata.normalize("NFKD", s)
    s = "".join(c for c in s if not unicodedata.combining(c))
    for ch in _APOSTROPHE_LIKE:
        s = s.replace(ch, "")
    s = re.sub(r"[^a-z0-9\s]", "", s)
    return re.sub(r"\s+", " ", s).strip()


def name_match_keys(name: str) -> list[str]:
    """
    Keys to try when linking a display name to fighters.name.
    For 3+ word names (e.g. Michael Venom Page), also try first + last (michael page).
    """
    keys: list[str] = []
    full = normalize_fighter_name(name)
    if not full:
        return keys
    keys.append(full)
    parts = full.split()
    if len(parts) >= 3:
        first_last = f"{parts[0]} {parts[-1]}"
        if first_last != full:
            keys.append(first_last)
    return keys


def build_fighter_name_lookup(conn: sqlite3.Connection) -> dict[str, int]:
    """Map name_match_keys(name) -> fighters.id (first wins on collision)."""
    lookup: dict[str, int] = {}
    for row in conn.execute("SELECT id, name FROM fighters"):
        fid, raw_name = int(row[0]), row[1]
        for key in name_match_keys(raw_name):
            if key not in lookup:
                lookup[key] = fid
    return lookup


def resolve_fighter_id_by_name(
    conn: sqlite3.Connection,
    name: str,
    *,
    lookup: dict[str, int] | None = None,
) -> int | None:
    keys = name_match_keys(name)
    if not keys:
        return None
    if lookup is not None:
        for key in keys:
            fid = lookup.get(key)
            if fid is not None:
                return fid
        return None
    row = conn.execute(
        "SELECT id FROM fighters WHERE lower(trim(name)) = ?",
        (name.strip().lower(),),
    ).fetchone()
    if row:
        return int(row["id"])
    for key in keys:
        for row in conn.execute("SELECT id, name FROM fighters"):
            if normalize_fighter_name(row["name"]) == key:
                return int(row["id"])
    return None


def scrape_ufc_rankings_rows() -> list[tuple[str, int, str]]:
    """Parse UFC meta rankings - returns (weight_class, rank, display_name)."""
    r = requests.get(UFC_RANKINGS_URL, headers=HEADERS, timeout=25)
    r.raise_for_status()
    time.sleep(DELAY)
    soup = BeautifulSoup(r.text, "html.parser")
    meta_view = soup.select_one(".view-display-id-meta_rankings")
    if meta_view is None:
        raise RuntimeError("Could not find UFC meta rankings section on rankings page")
    out: list[tuple[str, int, str]] = []
    for grp in meta_view.select(".view-grouping"):
        hdr = grp.select_one(".view-grouping-header")
        if not hdr:
            continue
        weight_class = re.sub(r"Top Rank.*$", "", hdr.get_text(strip=True), flags=re.I).strip()
        if not weight_class or "pound-for-pound" in weight_class.lower():
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
                rank_td = tr.select_one(".views-field-meta-weight-class-rank")
                title_a = tr.select_one(".views-field-title a")
                if not rank_td or not title_a:
                    continue
                rk_txt = rank_td.get_text(strip=True)
                if not rk_txt.isdigit():
                    continue
                out.append((weight_class, int(rk_txt), title_a.get_text(strip=True)))
    return out


def sync_fighter_rankings(conn: sqlite3.Connection) -> tuple[int, int]:
    """Replace fighter_rankings from ufc.com meta rankings. Returns (inserted, skipped_unmatched)."""
    conn.execute("DELETE FROM fighter_rankings")
    rows = scrape_ufc_rankings_rows()
    name_lookup = build_fighter_name_lookup(conn)
    now = int(datetime.now().timestamp())
    inserted = 0
    skipped = 0
    for wc, rk, name in rows:
        fid = resolve_fighter_id_by_name(conn, name, lookup=name_lookup)
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

    # Rankings changes affect momentum via quality-of-opposition. Recompute all
    # momentum scores so the database stays consistent after a rankings sync.
    refresh_all_fighter_momentum(conn)
    refresh_all_fighter_resume(conn)
    conn.commit()

    return inserted, skipped


# ─────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────

def main() -> None:
    global DELAY
    default_delay = DELAY
    parser = argparse.ArgumentParser(
        description="Scrape ufcstats.com into SQLite (fighters, fights, round stats; UFC.com meta rankings).",
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
    parser.add_argument(
        "--sync-recent",
        action="store_true",
        help="Scrape fighters on completed cards in a recent window (see --since-days). For post-event DB updates.",
    )
    parser.add_argument(
        "--since-days",
        type=int,
        default=14,
        metavar="N",
        help="With --sync-recent: include events whose date falls within the last N UTC days (default: 14).",
    )
    parser.add_argument(
        "--rankings-only",
        action="store_true",
        help="Only refresh fighter_rankings from ufc.com meta rankings (no ufcstats fighter/event scraping).",
    )
    args = parser.parse_args()

    if args.fighter_id and args.limit is not None:
        parser.error("Use either a fighter id or --limit, not both.")
    if args.sync_recent and args.fighter_id:
        parser.error("Use either --sync-recent or a fighter id, not both.")
    if args.sync_recent and args.limit is not None:
        parser.error("Use either --sync-recent or --limit, not both.")
    if args.since_days != 14 and not args.sync_recent:
        parser.error("--since-days requires --sync-recent.")
    if args.since_days < 1:
        parser.error("--since-days must be at least 1.")
    if args.rankings_only and (
        args.fighter_id or args.sync_recent or args.limit is not None
    ):
        parser.error("--rankings-only cannot be combined with a fighter id, --sync-recent, or --limit.")

    DELAY = max(0.0, args.delay)

    conn = get_connection(DB_PATH)

    try:
        if args.rankings_only:
            print("Syncing UFC.com meta rankings only ...")
            ins, sk = sync_fighter_rankings(conn)
            print(f"  {ins} rows stored, {sk} names not matched in local fighters table.")
            print(f"  Database: {DB_PATH}")

        elif args.sync_recent:
            sync_recent_events(conn, since_days=args.since_days)

        elif args.fighter_id:
            fighter_id = args.fighter_id.strip().lower()
            print(f"Scraping fighter: {fighter_id}")
            fighter, n_fights, _refreshed = persist_fighter_bundle(conn, fighter_id)
            print(f"  Saved: {fighter.name} - {n_fights} fights from profile")

            print()
            print("-" * 50)
            print(f"  {fighter.name}")
            print("-" * 50)
            print(f"  Stance:    {fighter.stance or 'N/A'}")
            print(f"  Height:    {fighter.height_cm} cm" if fighter.height_cm else "  Height:    N/A")
            print(f"  Reach:     {fighter.reach_cm} cm" if fighter.reach_cm else "  Reach:     N/A")
            dob = (
                format_unix_date(fighter.date_of_birth)
                if fighter.date_of_birth is not None
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
                    fighter, _n, _refreshed = persist_fighter_bundle(conn, fighter_id)
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

        if not args.rankings_only:
            try:
                ins, sk = sync_fighter_rankings(conn)
                print(f"  UFC.com meta rankings: {ins} rows stored, {sk} names not matched in local fighters table.")
            except Exception as exc:
                print(f"  [warn] rankings sync failed: {exc}")

    finally:
        close_archetype_store()
        conn.close()

    try:
        from upcoming_matchups import refresh_upcoming_matchups_cache

        print("Refreshing upcoming matchups cache ...")
        payload = refresh_upcoming_matchups_cache(Path(DB_PATH))
        n_events = len(payload.get("events") or [])
        print(f"  Upcoming matchups cache updated ({n_events} events).")
    except Exception as exc:
        print(f"  [warn] upcoming matchups refresh failed: {exc}")


if __name__ == "__main__":
    main()
