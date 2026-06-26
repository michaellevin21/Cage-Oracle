interface FighterNameProps {
  side: "a" | "b";
  children: React.ReactNode;
  className?: string;
}

export function fighterNameClass(side: "a" | "b"): string {
  return side === "a" ? "fighter-a-name" : "fighter-b-name";
}

export function FighterName({ side, children, className }: FighterNameProps) {
  const tone = fighterNameClass(side);
  const classes = className ? `${tone} ${className}` : tone;
  return <span className={classes}>{children}</span>;
}

export function fighterSideForName(
  name: string,
  nameA: string,
  nameB: string,
): "a" | "b" | null {
  if (name === nameA) return "a";
  if (name === nameB) return "b";
  return null;
}
