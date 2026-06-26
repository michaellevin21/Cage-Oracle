"""FastAPI server for the UFC Matchup Analyzer web UI."""

from __future__ import annotations

import sys
from pathlib import Path

from fastapi import FastAPI, HTTPException, Query
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles

_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(_ROOT / "python"))

from matchup_service import analyze_matchup, default_db_path, search_fighters  # noqa: E402

app = FastAPI(title="UFC Matchup Analyzer", version="1.0.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.get("/api/health")
def health() -> dict[str, str]:
    db = default_db_path()
    return {"status": "ok", "database": str(db), "database_exists": str(db.is_file())}


@app.get("/api/fighters/search")
def fighters_search(
    q: str = Query("", min_length=0, max_length=120),
    limit: int = Query(20, ge=1, le=50),
) -> list[dict]:
    db_path = default_db_path()
    if not db_path.is_file():
        raise HTTPException(status_code=503, detail=f"Database not found: {db_path}")
    return search_fighters(db_path, q, limit=limit)


@app.get("/api/matchup")
def matchup(
    fighter_a: str = Query(..., min_length=1, max_length=120),
    fighter_b: str = Query(..., min_length=1, max_length=120),
) -> dict:
    db_path = default_db_path()
    if not db_path.is_file():
        raise HTTPException(status_code=503, detail=f"Database not found: {db_path}")

    try:
        return analyze_matchup(fighter_a.strip(), fighter_b.strip(), db_path=db_path)
    except LookupError as exc:
        raise HTTPException(status_code=404, detail=str(exc)) from exc
    except FileNotFoundError as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc
    except RuntimeError as exc:
        raise HTTPException(status_code=500, detail=str(exc)) from exc


_frontend_dist = _ROOT / "frontend" / "dist"
if _frontend_dist.is_dir():
    app.mount("/", StaticFiles(directory=_frontend_dist, html=True), name="frontend")
