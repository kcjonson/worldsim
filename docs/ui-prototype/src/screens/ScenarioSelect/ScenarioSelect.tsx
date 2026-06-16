import { useState } from "react";
import { Button, Badge, Icon } from "../../design-system";
import { Starfield } from "../../scene/Starfield";
import { SCENARIOS } from "../../data/mock";
import type { ScreenProps } from "../types";
import styles from "./ScenarioSelect.module.css";

function difficultyColor(pip: number, difficulty: number): string {
	if (pip > difficulty) return "var(--text-faint)";
	if (difficulty <= 2) return "var(--status-ok)";
	if (difficulty <= 3) return "var(--status-warn)";
	return "var(--status-crit)";
}

export function ScenarioSelect({ go }: ScreenProps) {
	const [selected, setSelected] = useState("standard");

	return (
		<div className={styles.screen}>
			<Starfield seed={42} dim />

			<div className={styles.layout}>
				<header className={styles.header}>
					<div className={styles.kicker}>// New Game · Step 01 / 03</div>
					<h1 className={styles.title}>Select Scenario</h1>
					<p className={styles.subtitle}>
						Each scenario reshapes your wreck site, salvage, and the world you'll fight to survive.
					</p>
				</header>

				<div className={styles.grid}>
					{SCENARIOS.map((s) => (
						<button
							key={s.id}
							className={[styles.card, selected === s.id ? styles.cardSelected : ""].filter(Boolean).join(" ")}
							onClick={() => setSelected(s.id)}
							aria-pressed={selected === s.id}
						>
							<span className={`${styles.corner} ${styles.tl}`} />
							<span className={`${styles.corner} ${styles.tr}`} />
							<span className={`${styles.corner} ${styles.bl}`} />
							<span className={`${styles.corner} ${styles.br}`} />

							{selected === s.id && (
								<span className={styles.checkmark}>
									<Icon name="check" size={13} />
								</span>
							)}

							<div className={styles.cardIcon}>
								<Icon name={s.icon} size={28} strokeWidth={1.4} />
							</div>

							<div className={styles.cardName}>{s.name}</div>
							<p className={styles.cardBlurb}>{s.blurb}</p>

							<div className={styles.cardMeta}>
								<div className={styles.difficulty}>
									{[1, 2, 3, 4, 5].map((pip) => (
										<span
											key={pip}
											className={styles.pip}
											style={{ background: difficultyColor(pip, s.difficulty) }}
										/>
									))}
								</div>
								<span className={styles.party}>
									<Icon name="users" size={11} />
									{s.party}
								</span>
							</div>

							<div className={styles.tags}>
								{s.tags.map((tag) => (
									<Badge key={tag} tone="outline">
										{tag}
									</Badge>
								))}
							</div>
						</button>
					))}
				</div>

				<footer className={styles.footer}>
					<Button variant="secondary" icon="chevronLeft" onClick={() => go("menu")}>
						Back
					</Button>
					<Button variant="primary" stencil iconRight="arrowRight" onClick={() => go("party")}>
						Confirm Crew Briefing
					</Button>
				</footer>
			</div>
		</div>
	);
}
