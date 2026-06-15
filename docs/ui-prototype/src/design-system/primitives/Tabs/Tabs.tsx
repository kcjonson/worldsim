import { Icon, type IconName } from "../Icon/Icon";
import styles from "./Tabs.module.css";

export interface TabItem<T extends string> {
	value: T;
	label: string;
	icon?: IconName;
}

export interface TabsProps<T extends string> {
	tabs: TabItem<T>[];
	value: T;
	onChange?: (value: T) => void;
	className?: string;
}

export function Tabs<T extends string>({ tabs, value, onChange, className }: TabsProps<T>) {
	return (
		<div className={[styles.bar, className ?? ""].filter(Boolean).join(" ")} role="tablist">
			{tabs.map((t) => (
				<button
					key={t.value}
					role="tab"
					aria-selected={t.value === value}
					className={[styles.tab, t.value === value ? styles.active : ""].filter(Boolean).join(" ")}
					onClick={() => onChange?.(t.value)}
				>
					{t.icon && <Icon name={t.icon} size={14} />}
					{t.label}
				</button>
			))}
		</div>
	);
}
