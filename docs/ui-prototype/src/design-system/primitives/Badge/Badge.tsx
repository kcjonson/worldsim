import type { ReactNode } from "react";
import { Icon, type IconName } from "../Icon/Icon";
import styles from "./Badge.module.css";

export interface BadgeProps {
	children: ReactNode;
	tone?: "default" | "accent" | "data" | "ok" | "warn" | "crit" | "outline";
	icon?: IconName;
	dot?: boolean;
}

export function Badge({ children, tone = "default", icon, dot }: BadgeProps) {
	return (
		<span className={[styles.badge, styles[tone]].join(" ")}>
			{dot && <span className={styles.dot} />}
			{icon && <Icon name={icon} size={11} />}
			{children}
		</span>
	);
}
