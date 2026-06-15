import styles from "./KeyCap.module.css";

export function KeyCap({ children }: { children: string }) {
	return <kbd className={styles.key}>{children}</kbd>;
}
