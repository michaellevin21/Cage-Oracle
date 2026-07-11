import { useEffect, useId, useRef, useState } from "react";
import { useHelpPanelScope } from "./HelpPanelContext";

interface MetricHelpProps {
  title: string;
  wide?: boolean;
  /** Colors the popover title — A red, B blue (matches win probability). */
  titleTone?: "a" | "b";
  children: React.ReactNode;
}

function isHelpButtonInScope(target: EventTarget | null): boolean {
  if (!(target instanceof HTMLElement)) return false;
  return Boolean(
    target.closest(".metric-help-btn")?.closest("[data-help-panel-scope]"),
  );
}

export function MetricHelp({ title, wide, titleTone, children }: MetricHelpProps) {
  const helpId = useId();
  const [localOpen, setLocalOpen] = useState(false);
  const wrapRef = useRef<HTMLSpanElement>(null);
  const popoverRef = useRef<HTMLDivElement>(null);
  const popoverId = useId();
  const helpPanel = useHelpPanelScope();
  const open = helpPanel ? helpPanel.openId === helpId : localOpen;

  function setOpen(next: boolean) {
    if (helpPanel) {
      if (next) {
        helpPanel.openHelp(helpId);
      } else {
        helpPanel.closeHelp(helpId);
      }
      return;
    }
    setLocalOpen(next);
  }

  function toggleOpen() {
    setOpen(!open);
  }

  useEffect(() => {
    if (!open) return;
    function onDocPointerDown(e: MouseEvent) {
      if (wrapRef.current?.contains(e.target as Node)) return;
      if (isHelpButtonInScope(e.target)) return;
      setOpen(false);
    }
    document.addEventListener("mousedown", onDocPointerDown);
    return () => document.removeEventListener("mousedown", onDocPointerDown);
  }, [open, helpId, helpPanel]);

  // Once the popover is open (and any section-expand animation has settled),
  // scroll it fully into view if it extends past the viewport.
  useEffect(() => {
    if (!open) return;
    const prefersReducedMotion = window.matchMedia(
      "(prefers-reduced-motion: reduce)",
    ).matches;
    const timer = window.setTimeout(() => {
      popoverRef.current?.scrollIntoView({
        block: "nearest",
        inline: "nearest",
        behavior: prefersReducedMotion ? "auto" : "smooth",
      });
    }, 300);
    return () => window.clearTimeout(timer);
  }, [open]);

  return (
    <span className="metric-help-wrap" ref={wrapRef}>
      <button
        type="button"
        className="metric-help-btn"
        aria-label={`About ${title}`}
        aria-expanded={open}
        aria-controls={popoverId}
        onClick={toggleOpen}
      >
        ?
      </button>
      {open && (
        <div
          ref={popoverRef}
          className={`metric-help-popover${wide ? " metric-help-popover-wide" : ""}`}
          id={popoverId}
          role="tooltip"
        >
          <p
            className={[
              "metric-help-title",
              titleTone === "a" ? "metric-help-title-a" : "",
              titleTone === "b" ? "metric-help-title-b" : "",
            ]
              .filter(Boolean)
              .join(" ")}
          >
            {title}
          </p>
          <div className="metric-help-body">{children}</div>
        </div>
      )}
    </span>
  );
}
