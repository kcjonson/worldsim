import { useState } from "react";
import {
	Avatar,
	Badge,
	Button,
	Divider,
	Icon,
	Meter,
	Panel,
	Stat,
} from "../../design-system";
import { Starfield } from "../../scene/Starfield";
import { COLONISTS, type Colonist } from "../../data/mock";
import type { ScreenProps } from "../types";
import styles from "./PartySelect.module.css";

function moodLabel(mood: number): string {
	if (mood < 0.3) return "Distressed";
	if (mood < 0.55) return "Uneasy";
	if (mood < 0.75) return "Stable";
	return "Content";
}

function traitTone(tone: "good" | "bad" | "neutral"): "ok" | "crit" | "default" {
	if (tone === "good") return "ok";
	if (tone === "bad") return "crit";
	return "default";
}

function shuffleArray<T>(arr: T[]): T[] {
	const out = [...arr];
	for (let i = out.length - 1; i > 0; i--) {
		const j = Math.floor(Math.random() * (i + 1));
		[out[i], out[j]] = [out[j], out[i]];
	}
	return out;
}

export function PartySelect({ go }: ScreenProps) {
	const [roster, setRoster] = useState<Colonist[]>(COLONISTS);
	const [activeId, setActiveId] = useState<string>(COLONISTS[0].id);

	const active = roster.find((c) => c.id === activeId) ?? roster[0];

	function handleRandomize() {
		const shuffled = shuffleArray(roster);
		setRoster(shuffled);
		setActiveId(shuffled[0].id);
	}

	return (
		<div className={styles.screen}>
			<Starfield seed={57} dim />

			<div className={styles.layout}>
				<header className={styles.header}>
					<div className={styles.kicker}>// New Game · Step 02 / 03</div>
					<h1 className={styles.title}>Assemble the Crew</h1>
					<p className={styles.subtitle}>
						Three survivors walked away from the wreck. Learn who they are.
					</p>
				</header>

				<div className={styles.main}>
					{/* LEFT: roster column */}
					<div className={styles.roster}>
						<div className={styles.rosterList}>
							{roster.map((c) => {
								const isActive = c.id === activeId;
								return (
									<button
										key={c.id}
										className={[styles.rosterCard, isActive ? styles.rosterCardActive : ""].filter(Boolean).join(" ")}
										onClick={() => setActiveId(c.id)}
										aria-pressed={isActive}
									>
										<span className={`${styles.corner} ${styles.tl}`} />
										<span className={`${styles.corner} ${styles.tr}`} />
										<span className={`${styles.corner} ${styles.bl}`} />
										<span className={`${styles.corner} ${styles.br}`} />

										<Avatar seed={c.name} size={40} mood={c.mood} selected={isActive} />

										<div className={styles.rosterInfo}>
											<div className={styles.rosterName}>{c.name}</div>
											<div className={styles.rosterRole}>{c.role}</div>
										</div>

										<div className={styles.rosterMood}>
											<Meter value={c.mood} tone="auto" size="sm" />
											<span className={styles.rosterMoodLabel}>{moodLabel(c.mood)}</span>
										</div>
									</button>
								);
							})}
						</div>

						<div className={styles.rosterActions}>
							<Button variant="data" icon="dice" onClick={handleRandomize}>
								Randomize
							</Button>
							<span className={styles.addHint}>
								<Icon name="users" size={13} />
								3 / 3 slots
							</span>
						</div>
					</div>

					{/* RIGHT: detail panel */}
					<Panel
						variant="raised"
						accent="accent"
						corners
						className={styles.detail}
						flush
					>
						<div className={styles.detailInner}>
							<div className={styles.detailHero}>
								<Avatar seed={active.name} size={72} mood={active.mood} />
								<div className={styles.detailHeroText}>
									<div className={styles.detailName}>{active.name}</div>
									<div className={styles.detailRole}>{active.role}</div>
								</div>
							</div>

							<div className={styles.statRow}>
								<Stat label="Origin" value={active.origin} size="sm" />
								<Stat label="Age" value={active.age} unit=" yrs" size="sm" />
								<Stat
									label="Mood"
									value={moodLabel(active.mood)}
									tone={active.mood < 0.3 ? "crit" : active.mood < 0.55 ? "warn" : "ok"}
									size="sm"
								/>
							</div>

							<Divider label="Background" />

							<p className={styles.backstory}>{active.backstory}</p>

							<Divider label="Skills" />

							<div className={styles.skills}>
								{active.skills.map((skill) => (
									<div key={skill.name} className={styles.skillRow}>
										<div className={styles.skillLabel}>
											<Icon name={skill.icon} size={14} className={styles.skillIcon} />
											<span className={styles.skillName}>{skill.name}</span>
										</div>
										<Meter
											value={skill.level / 20}
											tone="accent"
											valueText={String(skill.level)}
											size="sm"
											className={styles.skillMeter}
										/>
									</div>
								))}
							</div>

							<Divider label="Traits" />

							<div className={styles.traits}>
								{active.traits.map((trait) => (
									<Badge key={trait.name} tone={traitTone(trait.tone)}>
										{trait.name}
									</Badge>
								))}
							</div>
						</div>
					</Panel>
				</div>

				<footer className={styles.footer}>
					<Button variant="secondary" icon="chevronLeft" onClick={() => go("scenario")}>
						Back
					</Button>
					<Button variant="primary" stencil iconRight="arrowRight" onClick={() => go("worldgen")}>
						Generate Planet
					</Button>
				</footer>
			</div>
		</div>
	);
}
