import type { ButtonHTMLAttributes, ReactNode } from "react";
import { Icon, type IconName } from "../Icon/Icon";
import styles from "./Button.module.css";

type Variant = "primary" | "secondary" | "ghost" | "danger" | "data";
type Size = "sm" | "md" | "lg";

export interface ButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
	variant?: Variant;
	size?: Size;
	icon?: IconName;
	iconRight?: IconName;
	iconOnly?: boolean;
	block?: boolean;
	/* uppercase, letter-spaced signage label */
	stencil?: boolean;
	children?: ReactNode;
}

export function Button({
	variant = "secondary",
	size = "md",
	icon,
	iconRight,
	iconOnly,
	block,
	stencil,
	className,
	children,
	...rest
}: ButtonProps) {
	const cls = [
		styles.btn,
		styles[variant],
		styles[size],
		iconOnly ? styles.iconOnly : "",
		block ? styles.block : "",
		stencil ? styles.stencil : "",
		className ?? "",
	]
		.filter(Boolean)
		.join(" ");

	const glyph = size === "lg" ? 18 : size === "sm" ? 13 : 15;

	return (
		<button className={cls} {...rest}>
			{icon && <Icon name={icon} size={glyph} />}
			{!iconOnly && children && <span className={styles.label}>{children}</span>}
			{iconOnly && !icon && children}
			{iconRight && <Icon name={iconRight} size={glyph} />}
		</button>
	);
}
