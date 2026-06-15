import { useState } from "react";
import { Button, Panel, Stat, Badge, Divider, Icon, SegmentedControl, type SegOption } from "../../design-system";
import { Starfield } from "../../scene/Starfield";
import { Planet, type PlanetMode } from "../../scene/Planet";
import { SAMPLE_SITE } from "../../data/mock";
import type { ScreenProps } from "../types";
import styles from "./LandingSite.module.css";

const VIZ: SegOption<PlanetMode>[] = [
	{ value: "biome", label: "Biomes", icon: "leaf" },
	{ value: "terrain", label: "Terrain", icon: "mountain" },
	{ value: "temp", label: "Temp", icon: "temp" },
	{ value: "precip", label: "Rain", icon: "rain" },
];

function coords(p: { x: number; y: number }): string {
	const lat = (0.5 - p.y) * 180;
	const lon = (p.x - 0.5) * 360;
	const ns = lat >= 0 ? "N" : "S";
	const ew = lon >= 0 ? "E" : "W";
	return `${Math.abs(lat).toFixed(1)}°${ns}  ${Math.abs(lon).toFixed(1)}°${ew}`;
}

export function LandingSite({ go }: ScreenProps) {
	const [mode, setMode] = useState<PlanetMode>("biome");
	const [marker, setMarker] = useState<{ x: number; y: number }>({ x: 0.43, y: 0.47 });
	const [confirming, setConfirming] = useState(false);

	const site = SAMPLE_SITE;

	return (
		<div className={styles.screen}>
			<Starfield seed={5} dim />

			<div className={styles.grid}>
				<header className={styles.head}>
					<div>
						<div className={styles.kicker}>// Expedition · Final Approach</div>
						<h1 className={styles.title}>Select Landing Site</h1>
					</div>
					<Button variant="ghost" icon="chevronLeft" onClick={() => go("worldgen")}>
						Back to Survey
					</Button>
				</header>

				<section className={styles.stage}>
					<Planet mode={mode} seed={2480} size={460} rotate={false} marker={marker} onPick={setMarker} />
					<div className={styles.crossHint}>
						<Icon name="crosshair" size={14} /> Click the surface to set your descent vector
					</div>
					<div className={styles.vizDock}>
						<SegmentedControl options={VIZ} value={mode} onChange={setMode} tone="data" size="sm" />
					</div>
				</section>

				<aside className={styles.info}>
					<Panel title="Landing Zone" kicker="Site Analysis" accent="accent" style={{ height: "100%" }} bodyClassName={styles.infoBody}>
						<div className={styles.coords}>
							<Icon name="crosshair" size={14} />
							{coords(marker)}
						</div>

						<h2 className={styles.biome}>{site.biome}</h2>
						<Badge tone="ok" icon="check">Recommended</Badge>

						<div className={styles.statRow}>
							<Stat label="Temp Range" value={site.temp} size="sm" />
							<Stat label="Rainfall" value={site.rainfall} size="sm" tone="data" />
						</div>

						<div className={styles.diffRow}>
							<span className={styles.diffLabel}>Difficulty</span>
							<div className={styles.skulls}>
								{[0, 1, 2, 3, 4].map((i) => (
									<Icon
										key={i}
										name="skull"
										size={15}
										style={{ color: i < site.difficulty ? "var(--status-warn)" : "var(--text-faint)" }}
									/>
								))}
							</div>
						</div>

						<Divider label="Field Report" />
						<p className={styles.notes}>{site.notes}</p>

						<Divider label="Hazards" />
						<div className={styles.hazards}>
							{site.hazards.map((h) => (
								<Badge key={h} tone="warn" icon="alert">
									{h}
								</Badge>
							))}
						</div>
					</Panel>
				</aside>

				<footer className={styles.foot}>
					<span className={styles.footNote}>You can land anywhere on solid ground. Choose well — there is no second descent.</span>
					<Button variant="primary" stencil icon="rocket" onClick={() => setConfirming(true)}>
						Confirm Landing Site
					</Button>
				</footer>
			</div>

			{confirming && (
				<div className={styles.modalScrim} onClick={() => setConfirming(false)}>
					<div className={styles.modalWrap} onClick={(e) => e.stopPropagation()}>
						<Panel title="Commit to Descent" kicker="Confirm" accent="accent" glow corners>
							<p className={styles.modalBody}>
								Your colony will begin at <strong>{coords(marker)}</strong> — {site.biome}. Once you commit, there is no
								turning back.
							</p>
							<div className={styles.modalActions}>
								<Button variant="secondary" onClick={() => setConfirming(false)}>
									Cancel
								</Button>
								<Button variant="primary" stencil icon="rocket" onClick={() => go("game")}>
									Begin Descent
								</Button>
							</div>
						</Panel>
					</div>
				</div>
			)}
		</div>
	);
}
