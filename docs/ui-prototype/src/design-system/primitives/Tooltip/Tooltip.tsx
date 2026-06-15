import type { ReactNode } from "react";
import styles from "./Tooltip.module.css";

export interface TooltipProps {
	content: ReactNode;
	children: ReactNode;
	side?: "top" | "bottom" | "left" | "right";
}

/* CSS-only hover/focus tooltip. Fine for a prototype; the C++ side has a real
 * TooltipManager with hover-delay and edge clamping. */
export function Tooltip({ content, children, side = "top" }: TooltipProps) {
	return (
		<span className={styles.wrap} tabIndex={0}>
			{children}
			<span className={[styles.bubble, styles[side]].join(" ")} role="tooltip">
				{content}
			</span>
		</span>
	);
}
