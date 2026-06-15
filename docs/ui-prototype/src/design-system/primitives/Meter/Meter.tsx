import styles from "./Meter.module.css";

type Tone = "accent" | "data" | "ok" | "warn" | "crit" | "auto";

export interface MeterProps {
	value: number; // 0..1
	label?: string;
	valueText?: string;
	tone?: Tone;
	segmented?: boolean;
	size?: "sm" | "md";
	/* render label + value INSIDE the bar (single element) instead of above it */
	inline?: boolean;
	className?: string;
}

function toneColor(tone: Tone, value: number): string {
	if (tone === "auto") {
		if (value < 0.25) return "var(--status-crit)";
		if (value < 0.5) return "var(--status-warn)";
		return "var(--status-ok)";
	}
	return {
		accent: "var(--accent)",
		data: "var(--data)",
		ok: "var(--status-ok)",
		warn: "var(--status-warn)",
		crit: "var(--status-crit)",
	}[tone];
}

export function Meter({ value, label, valueText, tone = "accent", segmented, size = "md", inline, className }: MeterProps) {
	const v = Math.max(0, Math.min(1, value));
	const color = toneColor(tone, v);

	if (inline) {
		return (
			<div className={[styles.inlineTrack, styles[size], className ?? ""].filter(Boolean).join(" ")}>
				<div
					className={styles.inlineFill}
					style={{
						width: `${v * 100}%`,
						background: `color-mix(in srgb, ${color} 22%, transparent)`,
						borderRight: `2px solid ${color}`,
						boxShadow: `0 0 8px ${color}`,
					}}
				/>
				<div className={styles.inlineOverlay}>
					{label && <span className={styles.inlineLabel}>{label}</span>}
					{valueText && (
						<span className={styles.inlineValue} style={{ color }}>
							{valueText}
						</span>
					)}
				</div>
			</div>
		);
	}

	return (
		<div className={[styles.wrap, styles[size], className ?? ""].filter(Boolean).join(" ")}>
			{(label || valueText) && (
				<div className={styles.row}>
					{label && <span className={styles.label}>{label}</span>}
					{valueText && <span className={styles.value} style={{ color }}>{valueText}</span>}
				</div>
			)}
			<div className={[styles.track, segmented ? styles.segmented : ""].filter(Boolean).join(" ")}>
				<div className={styles.fill} style={{ width: `${v * 100}%`, background: color, boxShadow: `0 0 8px ${color}` }} />
			</div>
		</div>
	);
}
