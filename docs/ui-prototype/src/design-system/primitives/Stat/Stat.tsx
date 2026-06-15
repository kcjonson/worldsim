import type { ReactNode } from "react";
import styles from "./Stat.module.css";

export interface StatProps {
	label: string;
	value: ReactNode;
	unit?: string;
	tone?: "default" | "accent" | "data" | "ok" | "warn" | "crit";
	align?: "left" | "right" | "center";
	size?: "sm" | "md" | "lg";
}

export function Stat({ label, value, unit, tone = "default", align = "left", size = "md" }: StatProps) {
	return (
		<div className={[styles.stat, styles[align], styles[size]].join(" ")}>
			<span className={styles.label}>{label}</span>
			<span className={[styles.value, styles[`t_${tone}`]].join(" ")}>
				{value}
				{unit && <span className={styles.unit}>{unit}</span>}
			</span>
		</div>
	);
}
