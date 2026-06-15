import { useEffect, type ReactNode } from "react";
import { Panel } from "../Panel/Panel";
import { Icon } from "../Icon/Icon";
import styles from "./Modal.module.css";

export interface ModalProps {
	open: boolean;
	onClose: () => void;
	title?: ReactNode;
	kicker?: ReactNode;
	size?: "sm" | "md" | "lg";
	accent?: "accent" | "data";
	footer?: ReactNode;
	children: ReactNode;
}

export function Modal({ open, onClose, title, kicker, size = "md", accent = "accent", footer, children }: ModalProps) {
	useEffect(() => {
		if (!open) return;
		// capture-phase + stopImmediatePropagation so Esc closes the modal
		// without also tripping the dev-chrome toggle on window
		const onKey = (e: KeyboardEvent) => {
			if (e.key === "Escape") {
				e.stopImmediatePropagation();
				onClose();
			}
		};
		window.addEventListener("keydown", onKey, true);
		return () => window.removeEventListener("keydown", onKey, true);
	}, [open, onClose]);

	if (!open) return null;

	return (
		<div className={styles.scrim} onClick={onClose}>
			<div className={[styles.wrap, styles[size]].join(" ")} onClick={(e) => e.stopPropagation()}>
				<Panel
					title={title}
					kicker={kicker}
					accent={accent}
					glow
					scanlines
					flush
					actions={
						<button className={styles.close} onClick={onClose} aria-label="Close">
							<Icon name="close" size={16} />
						</button>
					}
				>
					<div className={styles.content}>{children}</div>
					{footer && <div className={styles.footer}>{footer}</div>}
				</Panel>
			</div>
		</div>
	);
}
