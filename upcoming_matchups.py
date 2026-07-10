#!/usr/bin/env python3
"""Fetch upcoming UFC events and fights from ufcstats.com as JSON."""

from __future__ import annotations

import argparse
import json
import sqlite3
import sys
import time
from pathlib import Path

from scraper import (
    discover_upcoming_events,
    format_unix_date,
    get_connection,
    persist_fighter_bundle,
    scrape_event_fights,
    utc_day_start,
)

_ROOT = Path(__file__).resolve().parent
_DEFAULT_DB = _ROOT / "ufc.db"
_DEFAULT_CACHE = _ROOT / ".upcoming_matchups_cache.json"
_CACHE_TTL_SECONDS = 600
_cache_payload: dict | None = None
_cache_at: float = 0.0


def resolve_fighter_name(
    conn: sqlite3.Connection | None,
    ufc_id: str,
    scraped_name: str,
    *,
    sync_fighters: bool,
    synced_ids: set[str],
) -> str:
    if conn is None:
        return scraped_name

    ufc_id = ufc_id.lower()
    row = conn.execute(
        "SELECT name FROM fighters WHERE ufc_id = ?",
        (ufc_id,),
    ).fetchone()
    if row:
        return str(row[0])
    if not sync_fighters or ufc_id in synced_ids:
        return scraped_name

    synced_ids.add(ufc_id)
    print(f"  Syncing upcoming fighter {scraped_name} ({ufc_id}) ...", file=sys.stderr)
    fighter, _n_fights, _refreshed = persist_fighter_bundle(conn, ufc_id)
    return fighter.name


def fetch_upcoming_matchups(
    db_path: Path | None = None,
    *,
    sync_fighters: bool = True,
) -> dict:
    global _cache_payload, _cache_at

    now = time.time()
    if (
        not sync_fighters
        and _cache_payload is not None
        and now - _cache_at < _CACHE_TTL_SECONDS
    ):
        return _cache_payload

    today_start = utc_day_start()
    events = discover_upcoming_events()
    upcoming = [
        event
        for event in events
        if event.event_date is not None and event.event_date >= today_start
    ]
    upcoming.sort(key=lambda event: event.event_date or 0)

    conn: sqlite3.Connection | None = None
    if db_path is not None and db_path.exists():
        conn = get_connection(str(db_path))
    elif sync_fighters:
        raise RuntimeError("Database not found; cannot sync upcoming-card fighters")

    synced_ids: set[str] = set()
    payload_events: list[dict] = []
    try:
        for event in upcoming:
            fights = scrape_event_fights(event.ufc_event_id)
            matchups = [
                {
                    "fighter_a": resolve_fighter_name(
                        conn,
                        fight.fighter1_ufc_id,
                        fight.fighter1_name,
                        sync_fighters=sync_fighters,
                        synced_ids=synced_ids,
                    ),
                    "fighter_b": resolve_fighter_name(
                        conn,
                        fight.fighter2_ufc_id,
                        fight.fighter2_name,
                        sync_fighters=sync_fighters,
                        synced_ids=synced_ids,
                    ),
                    "weight_class": fight.weight_class,
                }
                for fight in fights
            ]
            if not matchups:
                continue
            payload_events.append({
                "event_id": event.ufc_event_id,
                "name": event.name,
                "event_date": (
                    format_unix_date(event.event_date)
                    if event.event_date is not None
                    else None
                ),
                "matchups": matchups,
            })
    finally:
        if conn is not None:
            conn.close()

    payload = {"events": payload_events}
    _cache_payload = payload
    _cache_at = now
    return payload


def refresh_upcoming_matchups_cache(
    db_path: Path | None = None,
    *,
    output_path: Path | None = None,
    sync_fighters: bool = True,
) -> dict:
    db_path = db_path or _DEFAULT_DB
    output_path = output_path or _DEFAULT_CACHE
    payload = fetch_upcoming_matchups(
        db_path if db_path.exists() else None,
        sync_fighters=sync_fighters,
    )
    output_path.write_text(json.dumps(payload), encoding="utf-8")
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--db",
        type=Path,
        default=_DEFAULT_DB,
        help="SQLite database for canonical fighter names (default: ufc.db)",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Print JSON to stdout",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Write JSON to this file instead of stdout",
    )
    parser.add_argument(
        "--no-sync-fighters",
        action="store_true",
        help="Do not scrape missing fighters into the database",
    )
    args = parser.parse_args()

    sync_fighters = not args.no_sync_fighters

    try:
        if args.output:
            payload = refresh_upcoming_matchups_cache(
                args.db,
                output_path=args.output,
                sync_fighters=sync_fighters,
            )
        else:
            payload = fetch_upcoming_matchups(
                args.db if args.db.exists() else None,
                sync_fighters=sync_fighters,
            )
    except Exception as exc:
        payload = {"events": [], "detail": str(exc)}

    if args.json or args.output:
        if not args.output:
            encoded = json.dumps(payload)
            print(encoded, flush=True)
        return 0 if "detail" not in payload else 1
    else:
        for event in payload["events"]:
            print(f"{event['name']} — {event.get('event_date') or 'Date TBD'}")
            for matchup in event["matchups"]:
                wc = f" ({matchup['weight_class']})" if matchup.get("weight_class") else ""
                print(
                    f"  {matchup['fighter_a']} vs {matchup['fighter_b']}{wc}",
                )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
