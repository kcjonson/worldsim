import styles from "./Divider.module.css";

export interface DividerProps {
	label?: string;
	className?: string;
}

export function Divider({ label, className }: DividerProps) {
	if (!label) return <hr className={[styles.line, className ?? ""].filter(Boolean).join(" ")} />;
	return (
		<div className={[styles.labeled, className ?? ""].filter(Boolean).join(" ")}>
			<span className={styles.seg} />
			<span className={styles.text}>{label}</span>
			<span className={styles.seg} />
		</div>
	);
}
