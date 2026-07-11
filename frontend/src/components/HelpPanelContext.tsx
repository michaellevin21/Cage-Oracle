import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useState,
  type ReactNode,
} from "react";

interface HelpPanelContextValue {
  openId: string | null;
  openHelp: (id: string) => void;
  closeHelp: (id: string) => void;
  /** True when opening this help will grow the section (first help in scope). */
  sectionExpanding: boolean;
}

const HelpPanelContext = createContext<HelpPanelContextValue | null>(null);

interface HelpPanelScopeProps {
  children: ReactNode;
  onAnyOpenChange: (open: boolean) => void;
}

export function HelpPanelScope({
  children,
  onAnyOpenChange,
}: HelpPanelScopeProps) {
  const [openId, setOpenId] = useState<string | null>(null);
  const [sectionExpanding, setSectionExpanding] = useState(false);

  useEffect(() => {
    onAnyOpenChange(openId !== null);
  }, [openId, onAnyOpenChange]);

  const openHelp = useCallback((id: string) => {
    setOpenId((current) => {
      setSectionExpanding(current === null);
      return id;
    });
  }, []);

  const closeHelp = useCallback((id: string) => {
    setOpenId((current) => {
      if (current === id) {
        setSectionExpanding(false);
        return null;
      }
      return current;
    });
  }, []);

  const value = useMemo(
    () => ({ openId, openHelp, closeHelp, sectionExpanding }),
    [openId, openHelp, closeHelp, sectionExpanding],
  );

  return (
    <HelpPanelContext.Provider value={value}>
      <div className="help-panel-scope" data-help-panel-scope="">
        {children}
      </div>
    </HelpPanelContext.Provider>
  );
}

export function useHelpPanelScope() {
  return useContext(HelpPanelContext);
}
