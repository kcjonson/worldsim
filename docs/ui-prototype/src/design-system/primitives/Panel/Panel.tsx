import type { CSSProperties, ReactNode } from "react";
import styles from "./Panel.module.css";

type Variant = "panel" | "raised" | "inset";
type Accent = "accent" | "data" | "none";

export interface PanelProps {
	children?: ReactNode;
	title?: ReactNode;
	/* small kicker label above/within the title bar */
	kicker?: ReactNode;
	actions?: ReactNode;
	variant?: Variant;
	accent?: Accent;
	/* draw L-bracket corner ticks */
	corners?: boolean;
	scanlines?: boolean;
	glow?: boolean;
	/* tighter header + body padding for dense contexts (HUD panels) */
	compact?: boolean;
	className?: string;
	bodyClassName?: string;
	style?: CSSProperties;
	/* remove body padding (for edge-to-edge content like lists/maps) */
	flush?: boolean;
}

export function Panel({
	children,
	title,
	kicker,
	actions,
	variant = "panel",
	accent = "accent",
	corners = true,
	scanlines = false,
	glow = false,
	compact = false,
	className,
	bodyClassName,
	style,
	flush,
}: PanelProps) {
	const cls = [
		styles.panel,
		styles[variant],
		styles[`a_${accent}`],
		glow ? styles.glow : "",
		compact ? styles.compact : "",
		scanlines ? "fx-scanlines" : "",
		className ?? "",
	]
		.filter(Boolean)
		.join(" ");

	return (
		<section className={cls} style={style}>
			{corners && (
				<>
					<span className={`${styles.corner} ${styles.tl}`} />
					<span className={`${styles.corner} ${styles.tr}`} />
					<span className={`${styles.corner} ${styles.bl}`} />
					<span className={`${styles.corner} ${styles.br}`} />
				</>
			)}
			{(title || actions || kicker) && (
				<header className={styles.head}>
					<div className={styles.headText}>
						{kicker && <span className={styles.kicker}>{kicker}</span>}
						{title && <h3 className={styles.title}>{title}</h3>}
					</div>
					{actions && <div className={styles.actions}>{actions}</div>}
				</header>
			)}
			<div className={[styles.body, flush ? styles.flush : "", bodyClassName ?? ""].filter(Boolean).join(" ")}>
				{children}
			</div>
		</section>
	);
}
