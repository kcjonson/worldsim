import { Icon, type IconName } from "../../design-system";
import { Starfield } from "../../scene/Starfield";
import { Planet } from "../../scene/Planet";
import { BRAND } from "../../data/mock";
import type { ScreenId, ScreenProps } from "../types";
import styles from "./MainMenu.module.css";

interface MenuEntry {
	label: string;
	icon: IconName;
	hint: string;
	to?: ScreenId;
	primary?: boolean;
	disabled?: boolean;
}

export function MainMenu({ go }: ScreenProps) {
	const entries: MenuEntry[] = [
		{ label: "New Game", icon: "play", hint: "Begin a new expedition", to: "scenario", primary: true },
		{ label: "Continue", icon: "refresh", hint: "Resume your last colony", to: "game" },
		{ label: "Load Game", icon: "save", hint: "Restore a saved expedition" },
		{ label: "Settings", icon: "gear", hint: "Graphics, audio, controls" },
		{ label: "Credits", icon: "users", hint: "The crew behind the crew" },
		{ label: "Exit", icon: "close", hint: "Quit to desktop" },
	];

	return (
		<div className={styles.screen}>
			<Starfield seed={11} />
			<Planet seed={108} size={640} mode="biome" className={styles.planet} />
			<div className={styles.planetScrim} />

			<div className={styles.content}>
				<header className={styles.head}>
					<div className={styles.mark}>◈</div>
					<div>
						<h1 className={styles.title}>{BRAND.title}</h1>
						<div className={styles.tagline}>{BRAND.tagline}</div>
					</div>
				</header>

				<nav className={styles.menu}>
					<div className={styles.kicker}>// Main Menu</div>
					{entries.map((e) => (
						<button
							key={e.label}
							className={[styles.item, e.primary ? styles.primary : "", e.disabled ? styles.disabled : ""].filter(Boolean).join(" ")}
							onClick={() => e.to && go(e.to)}
							disabled={e.disabled}
						>
							<span className={styles.bracket}>›</span>
							<Icon name={e.icon} size={18} className={styles.itemIcon} />
							<span className={styles.itemLabel}>{e.label}</span>
							<span className={styles.itemHint}>{e.hint}</span>
						</button>
					))}
				</nav>
			</div>

			<footer className={styles.footer}>
				<span>{BRAND.version}</span>
				<span className={styles.foothint}>Star system: Maed · Sector 28-B</span>
			</footer>
		</div>
	);
}
