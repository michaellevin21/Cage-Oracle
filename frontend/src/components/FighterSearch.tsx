import { useEffect, useId, useRef, useState, type KeyboardEvent } from "react";
import { searchFighters } from "../api";
import { fighterNameClass } from "./FighterName";
import type { FighterSummary } from "../types";

interface FighterSearchProps {
  label: string;
  side?: "a" | "b";
  value: string;
  onChange: (name: string) => void;
  disabled?: boolean;
}

export function FighterSearch({
  label,
  side,
  value,
  onChange,
  disabled,
}: FighterSearchProps) {
  const [query, setQuery] = useState(value);
  const [results, setResults] = useState<FighterSummary[]>([]);
  const [open, setOpen] = useState(false);
  const [loading, setLoading] = useState(false);
  const [activeIndex, setActiveIndex] = useState(-1);
  const wrapRef = useRef<HTMLDivElement>(null);
  const listRef = useRef<HTMLUListElement>(null);
  const listboxId = useId();

  const showList = open && query.trim().length >= 2;
  const canNavigate = showList && !loading && results.length > 0;

  useEffect(() => {
    if (value) {
      setQuery(value);
    }
  }, [value]);

  useEffect(() => {
    if (!open || query.trim().length < 2) {
      setResults([]);
      return;
    }

    const timer = window.setTimeout(() => {
      setLoading(true);
      searchFighters(query)
        .then(setResults)
        .catch(() => setResults([]))
        .finally(() => setLoading(false));
    }, 200);

    return () => window.clearTimeout(timer);
  }, [query, open]);

  useEffect(() => {
    setActiveIndex(results.length > 0 ? 0 : -1);
  }, [results]);

  useEffect(() => {
    if (activeIndex < 0 || !listRef.current) return;
    const buttons = listRef.current.querySelectorAll("button.suggestion");
    buttons[activeIndex]?.scrollIntoView({ block: "nearest" });
  }, [activeIndex, results]);

  useEffect(() => {
    function onDocClick(e: MouseEvent) {
      if (wrapRef.current && !wrapRef.current.contains(e.target as Node)) {
        setOpen(false);
      }
    }
    document.addEventListener("mousedown", onDocClick);
    return () => document.removeEventListener("mousedown", onDocClick);
  }, []);

  function select(fighter: FighterSummary) {
    onChange(fighter.name);
    setQuery(fighter.name);
    setOpen(false);
    setActiveIndex(-1);
  }

  function handleKeyDown(e: KeyboardEvent<HTMLInputElement>) {
    if (e.key === "Escape") {
      if (open) {
        e.preventDefault();
        setOpen(false);
        setActiveIndex(-1);
      }
      return;
    }

    if (!canNavigate) return;

    switch (e.key) {
      case "ArrowDown":
        e.preventDefault();
        setActiveIndex((i) => Math.min(i + 1, results.length - 1));
        break;
      case "ArrowUp":
        e.preventDefault();
        setActiveIndex((i) => Math.max(i - 1, 0));
        break;
      case "Enter":
        if (activeIndex >= 0 && activeIndex < results.length) {
          e.preventDefault();
          select(results[activeIndex]);
        }
        break;
    }
  }

  return (
    <div className="fighter-search" ref={wrapRef}>
      <label
        className={[
          "field-label",
          side ? fighterNameClass(side) : "",
        ]
          .filter(Boolean)
          .join(" ")}
        htmlFor={listboxId}
      >
        {label}
      </label>
      <input
        id={listboxId}
        type="text"
        className="fighter-input"
        value={query}
        disabled={disabled}
        placeholder="Start typing a name…"
        role="combobox"
        aria-expanded={showList}
        aria-controls={`${listboxId}-list`}
        aria-autocomplete="list"
        aria-activedescendant={
          canNavigate && activeIndex >= 0
            ? `${listboxId}-option-${activeIndex}`
            : undefined
        }
        onChange={(e) => {
          const next = e.target.value;
          setQuery(next);
          setOpen(true);
          if (value && next !== value) {
            onChange("");
          }
        }}
        onFocus={() => setOpen(true)}
        onKeyDown={handleKeyDown}
        autoComplete="off"
      />
      {showList && (
        <ul
          className="suggestions"
          id={`${listboxId}-list`}
          ref={listRef}
          role="listbox"
        >
          {loading && <li className="suggestion muted">Searching…</li>}
          {!loading && results.length === 0 && (
            <li className="suggestion muted">No fighters found</li>
          )}
          {results.map((f, index) => (
            <li key={f.name} role="presentation">
              <button
                type="button"
                id={`${listboxId}-option-${index}`}
                className={`suggestion${index === activeIndex ? " suggestion-active" : ""}`}
                role="option"
                aria-selected={index === activeIndex}
                onMouseEnter={() => setActiveIndex(index)}
                onClick={() => select(f)}
              >
                <span className="suggestion-name">{f.name}</span>
                {f.weight_class && (
                  <span className="suggestion-wc">{f.weight_class}</span>
                )}
              </button>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
