import { useCallback, useEffect, useRef, useState } from "react";

interface ScrollHint<T extends HTMLElement> {
  ref: React.RefObject<T | null>;
  /** True when there is more content hidden to the left. */
  overflowStart: boolean;
  /** True when there is more content hidden to the right. */
  overflowEnd: boolean;
}

/**
 * Tracks horizontal overflow of a scroll container so callers can render
 * edge cues (fades / chevrons) only when there is more content to scroll to.
 */
export function useScrollHint<T extends HTMLElement>(): ScrollHint<T> {
  const ref = useRef<T | null>(null);
  const [overflowStart, setOverflowStart] = useState(false);
  const [overflowEnd, setOverflowEnd] = useState(false);

  const update = useCallback(() => {
    const el = ref.current;
    if (!el) return;
    const { scrollLeft, scrollWidth, clientWidth } = el;
    const maxScroll = scrollWidth - clientWidth;
    const scrollable = maxScroll > 1;
    setOverflowStart(scrollable && scrollLeft > 1);
    setOverflowEnd(scrollable && scrollLeft < maxScroll - 1);
  }, []);

  useEffect(() => {
    const el = ref.current;
    if (!el) return;

    update();
    el.addEventListener("scroll", update, { passive: true });

    const observer = new ResizeObserver(update);
    observer.observe(el);
    const child = el.firstElementChild;
    if (child) observer.observe(child);

    window.addEventListener("resize", update);

    return () => {
      el.removeEventListener("scroll", update);
      observer.disconnect();
      window.removeEventListener("resize", update);
    };
  }, [update]);

  return { ref, overflowStart, overflowEnd };
}
