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

  useEffect(() => {
    onAnyOpenChange(openId !== null);
  }, [openId, onAnyOpenChange]);

  const openHelp = useCallback((id: string) => {
    setOpenId(id);
  }, []);

  const closeHelp = useCallback((id: string) => {
    setOpenId((current) => (current === id ? null : current));
  }, []);

  const value = useMemo(
    () => ({ openId, openHelp, closeHelp }),
    [openId, openHelp, closeHelp],
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
