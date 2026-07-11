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

function scrollPopoverIntoView(popover: HTMLElement) {
  const prefersReducedMotion = window.matchMedia(
    "(prefers-reduced-motion: reduce)",
  ).matches;
  popover.scrollIntoView({
    block: "nearest",
    inline: "nearest",
    behavior: prefersReducedMotion ? "auto" : "smooth",
  });
}

/** Run `run` after the comparison section expand animation, or on the next frame if none. */
function afterSectionExpand(
  section: HTMLElement | null,
  waitForExpand: boolean,
  run: () => void,
): () => void {
  if (!section || !waitForExpand) {
    const frame = requestAnimationFrame(run);
    return () => cancelAnimationFrame(frame);
  }

  let done = false;
  const finish = () => {
    if (done) return;
    done = true;
    run();
  };

  const onTransitionEnd = (e: TransitionEvent) => {
    if (e.target !== section) return;
    if (e.propertyName !== "min-height" && e.propertyName !== "padding-bottom") {
      return;
    }
    finish();
  };

  section.addEventListener("transitionend", onTransitionEnd);
  const fallback = window.setTimeout(finish, 280);

  return () => {
    done = true;
    section.removeEventListener("transitionend", onTransitionEnd);
    window.clearTimeout(fallback);
  };
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

  // Scroll the popover into view once layout is ready (after section expand, if any).
  useEffect(() => {
    if (!open) return;
    const popover = popoverRef.current;
    if (!popover) return;

    const section = popover.closest<HTMLElement>(".comparison-section");
    const waitForExpand = helpPanel?.sectionExpanding ?? false;

    return afterSectionExpand(section, waitForExpand, () => {
      scrollPopoverIntoView(popover);
    });
  }, [open, helpPanel?.sectionExpanding]);

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
