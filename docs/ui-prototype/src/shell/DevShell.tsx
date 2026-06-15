import type { ReactNode } from "react";
import { Icon } from "../design-system";
import { BRAND } from "../data/mock";
import type { ScreenId } from "../screens/types";
import styles from "./DevShell.module.css";

interface NavItem {
	id: ScreenId;
	label: string;
	step?: number;
}
interface NavGroup {
	label: string;
	items: NavItem[];
}

const GROUPS: NavGroup[] = [
	{
		label: "Pre-Game Flow",
		items: [
			{ id: "splash", label: "Splash / Loading", step: 1 },
			{ id: "menu", label: "Main Menu", step: 2 },
			{ id: "scenario", label: "Scenario Select", step: 3 },
			{ id: "party", label: "Party Selection", step: 4 },
			{ id: "worldgen", label: "Planet Generation", step: 5 },
			{ id: "landing", label: "Landing Site", step: 6 },
		],
	},
	{
		label: "In-Game",
		items: [{ id: "game", label: "HUD & Panels" }],
	},
	{
		label: "Design System",
		items: [{ id: "components", label: "Component Gallery" }],
	},
];

export interface DevShellProps {
	screen: ScreenId;
	onNavigate: (id: ScreenId) => void;
	chromeHidden: boolean;
	onToggleChrome: () => void;
	children: ReactNode;
}

export function DevShell({ screen, onNavigate, chromeHidden, onToggleChrome, children }: DevShellProps) {
	return (
		<div className={[styles.shell, chromeHidden ? styles.hidden : ""].filter(Boolean).join(" ")}>
			<aside className={styles.rail}>
				<div className={styles.brand}>
					<span className={styles.brandMark}>◈</span>
					<div>
						<div className={styles.brandTitle}>{BRAND.title}</div>
						<div className={styles.brandSub}>Salvage · UI Prototype</div>
					</div>
				</div>

				<nav className={styles.nav}>
					{GROUPS.map((g) => (
						<div key={g.label} className={styles.group}>
							<div className={styles.groupLabel}>{g.label}</div>
							{g.items.map((it) => (
								<button
									key={it.id}
									data-screen={it.id}
									className={[styles.navItem, screen === it.id ? styles.navActive : ""].filter(Boolean).join(" ")}
									onClick={() => onNavigate(it.id)}
								>
									{it.step !== undefined && <span className={styles.step}>{String(it.step).padStart(2, "0")}</span>}
									<span className={styles.navLabel}>{it.label}</span>
								</button>
							))}
						</div>
					))}
				</nav>

				<button className={styles.hideBtn} onClick={onToggleChrome}>
					<Icon name="eye" size={14} />
					Hide chrome <span className={styles.kbd}>Esc</span>
				</button>
			</aside>

			<main className={styles.stage}>
				<div className={styles.viewport}>{children}</div>
			</main>

			{chromeHidden && (
				<button className={styles.restore} onClick={onToggleChrome} title="Show dev chrome (Esc)">
					<Icon name="menu" size={16} />
				</button>
			)}
		</div>
	);
}
