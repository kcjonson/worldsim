import { Icon, type IconName } from "../Icon/Icon";
import styles from "./SegmentedControl.module.css";

export interface SegOption<T extends string> {
	value: T;
	label: string;
	icon?: IconName;
}

export interface SegmentedControlProps<T extends string> {
	options: SegOption<T>[];
	value: T;
	onChange?: (value: T) => void;
	size?: "sm" | "md";
	tone?: "accent" | "data";
	className?: string;
}

export function SegmentedControl<T extends string>({
	options,
	value,
	onChange,
	size = "md",
	tone = "accent",
	className,
}: SegmentedControlProps<T>) {
	return (
		<div className={[styles.group, styles[size], styles[tone], className ?? ""].filter(Boolean).join(" ")} role="tablist">
			{options.map((opt) => (
				<button
					key={opt.value}
					role="tab"
					aria-selected={opt.value === value}
					className={[styles.seg, opt.value === value ? styles.active : ""].filter(Boolean).join(" ")}
					onClick={() => onChange?.(opt.value)}
				>
					{opt.icon && <Icon name={opt.icon} size={size === "sm" ? 13 : 15} />}
					{opt.label}
				</button>
			))}
		</div>
	);
}
