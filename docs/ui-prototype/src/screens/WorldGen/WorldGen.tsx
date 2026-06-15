import { useEffect, useState } from "react";
import { Button, Panel, Slider, SegmentedControl, Stat, Meter, Icon, Badge, type SegOption } from "../../design-system";
import { Starfield } from "../../scene/Starfield";
import { Planet, type PlanetMode } from "../../scene/Planet";
import { WORLD_PRESETS, WORLD_STATS, CLIMATE_BREAKDOWN } from "../../data/mock";
import type { ScreenProps } from "../types";
import styles from "./WorldGen.module.css";

type Phase = "setup" | "generating" | "done";

interface Params {
	water: number;
	plates: number;
	atmosphere: number;
	dayLength: number;
	age: number;
	temp: number;
}

const DEFAULT_PARAMS: Params = { water: 62, plates: 9, atmosphere: 1.0, dayLength: 26, age: 4.5, temp: 14 };

const PRESET_PARAMS: Record<string, Partial<Params>> = {
	earthlike: { water: 62, temp: 14, atmosphere: 1.0 },
	desert: { water: 18, temp: 31, atmosphere: 0.7 },
	ocean: { water: 91, temp: 19, atmosphere: 1.3 },
	frozen: { water: 55, temp: -14, atmosphere: 0.6 },
	volcanic: { water: 34, temp: 27, atmosphere: 1.8 },
	garden: { water: 58, temp: 21, atmosphere: 1.1 },
};

const VIZ: SegOption<PlanetMode>[] = [
	{ value: "terrain", label: "Terrain", icon: "mountain" },
	{ value: "biome", label: "Biomes", icon: "leaf" },
	{ value: "temp", label: "Temp", icon: "temp" },
	{ value: "precip", label: "Rain", icon: "rain" },
];

const GEN_PHASES = [
	"Generating tectonic plates",
	"Simulating plate movement",
	"Raising terrain from collisions",
	"Modeling atmospheric circulation",
	"Calculating precipitation & rivers",
	"Forming oceans and seas",
	"Assigning biomes",
	"Calculating snow and glaciers",
	"Finalizing world data",
];

export function WorldGen({ go }: ScreenProps) {
	const [phase, setPhase] = useState<Phase>("setup");
	const [progress, setProgress] = useState(0);
	const [mode, setMode] = useState<PlanetMode>("biome");
	const [preset, setPreset] = useState("earthlike");
	const [seed, setSeed] = useState(2480);
	const [params, setParams] = useState<Params>(DEFAULT_PARAMS);

	const set = (k: keyof Params) => (v: number) => setParams((p) => ({ ...p, [k]: v }));

	const applyPreset = (id: string) => {
		setPreset(id);
		setParams({ ...DEFAULT_PARAMS, ...PRESET_PARAMS[id] });
	};

	const randomize = () => setSeed(Math.floor(1000 + Math.random() * 8999));

	const generate = () => {
		setPhase("generating");
		setProgress(0);
	};

	useEffect(() => {
		if (phase !== "generating") return;
		const id = setInterval(() => {
			setProgress((p) => {
				const next = p + 0.9 + Math.random() * 1.6;
				if (next >= 100) {
					clearInterval(id);
					setPhase("done");
					return 100;
				}
				return next;
			});
		}, 55);
		return () => clearInterval(id);
	}, [phase]);

	const genPhase = GEN_PHASES[Math.min(GEN_PHASES.length - 1, Math.floor((progress / 100) * GEN_PHASES.length))];
	const planetProgress = phase === "setup" ? 1 : progress / 100;
	const habitability = 4; // of 5

	return (
		<div className={styles.screen}>
			<Starfield seed={5} dim />

			<div className={styles.grid}>
				<header className={styles.head}>
					<div>
						<div className={styles.kicker}>// New Game · Step 03 / 03</div>
						<h1 className={styles.title}>Generate Planet</h1>
					</div>
					<Button variant="ghost" iconOnly icon="close" onClick={() => go("party")} aria-label="Back" />
				</header>

				{/* parameters */}
				<aside className={styles.params}>
					<Panel title="Parameters" kicker="Survey Config" accent="data" style={{ height: "100%" }} bodyClassName={styles.paramBody}>
						<div className={styles.presets}>
							{WORLD_PRESETS.map((p) => (
								<button
									key={p.id}
									className={[styles.preset, preset === p.id ? styles.presetActive : ""].filter(Boolean).join(" ")}
									onClick={() => applyPreset(p.id)}
								>
									<Icon name={p.icon} size={15} />
									{p.name}
								</button>
							))}
						</div>

						<div className={styles.sliders}>
							<Slider label="Water Coverage" value={params.water} min={0} max={100} unit="%" detent={0.62} onChange={set("water")} />
							<Slider label="Tectonic Plates" value={params.plates} min={2} max={30} detent={0.28} onChange={set("plates")} />
							<Slider label="Atmosphere" value={params.atmosphere} min={0.1} max={3} step={0.1} detent={0.31} onChange={set("atmosphere")} format={(v) => `${v.toFixed(1)} atm`} />
							<Slider label="Day Length" value={params.dayLength} min={8} max={48} unit="h" detent={0.45} onChange={set("dayLength")} />
							<Slider label="Planet Age" value={params.age} min={0.5} max={10} step={0.1} detent={0.45} onChange={set("age")} format={(v) => `${v.toFixed(1)} Gy`} />
							<Slider label="Mean Temp" value={params.temp} min={-20} max={40} detent={0.57} onChange={set("temp")} format={(v) => `${v}°C`} />
						</div>

						<div className={styles.seedRow}>
							<div className={styles.seedBox}>
								<span className={styles.seedLabel}>Seed</span>
								<span className={styles.seedVal}>{seed}</span>
							</div>
							<Button variant="secondary" iconOnly icon="dice" onClick={randomize} aria-label="Randomize seed" />
						</div>
					</Panel>
				</aside>

				{/* planet stage */}
				<section className={styles.stage}>
					<Planet mode={mode} seed={seed} size={440} rotate scan={phase === "generating"} progress={planetProgress} />

					{phase === "setup" && <div className={styles.stageHint}>Configure the survey, then run generation.</div>}

					{phase !== "setup" && (
						<div className={styles.vizDock}>
							<SegmentedControl options={VIZ} value={mode} onChange={setMode} tone="data" size="sm" />
						</div>
					)}

					{phase === "generating" && (
						<div className={styles.progress}>
							<div className={styles.progressHead}>
								<span className={styles.genPhase}>
									<span className={styles.spinner} /> {genPhase}…
								</span>
								<span className={styles.genPct}>{Math.floor(progress)}%</span>
							</div>
							<div className={styles.progressTrack}>
								<div className={styles.progressFill} style={{ width: `${progress}%` }} />
							</div>
						</div>
					)}
				</section>

				{/* info / readout */}
				<aside className={styles.info}>
					<Panel title="World Survey" kicker={phase === "done" ? "Complete" : "Pending"} style={{ height: "100%" }} bodyClassName={styles.infoBody}>
						{phase !== "done" ? (
							<div className={styles.infoEmpty}>
								<Icon name="globe" size={32} />
								<p>Survey data resolves as the world is generated.</p>
							</div>
						) : (
							<>
								<div className={styles.statGrid}>
									{WORLD_STATS.map((s) => (
										<Stat key={s.label} label={s.label} value={s.value} tone={s.tone} size="sm" />
									))}
								</div>

								<div className={styles.habit}>
									<span className={styles.habitLabel}>Habitability</span>
									<div className={styles.stars}>
										{[0, 1, 2, 3, 4].map((i) => (
											<Icon key={i} name="star" size={16} filled style={{ color: i < habitability ? "var(--accent)" : "var(--text-faint)" }} />
										))}
									</div>
								</div>

								<div className={styles.climate}>
									<div className={styles.climateHead}>Climate Distribution</div>
									{CLIMATE_BREAKDOWN.map((c) => (
										<Meter key={c.label} label={c.label} value={c.pct} valueText={`${Math.round(c.pct * 100)}%`} tone="data" size="sm" />
									))}
								</div>

								<div className={styles.hazards}>
									<Badge tone="ok" icon="check">Water: Good</Badge>
									<Badge tone="ok" icon="check">Arable: 25%</Badge>
									<Badge tone="warn" icon="alert">Storms: Moderate</Badge>
								</div>
							</>
						)}
					</Panel>
				</aside>

				{/* footer actions */}
				<footer className={styles.foot}>
					<Button variant="secondary" icon="chevronLeft" onClick={() => go("party")}>
						Back
					</Button>

					<div className={styles.footStatus}>
						{phase === "setup" && "Awaiting generation"}
						{phase === "generating" && "Generating world…"}
						{phase === "done" && (
							<span className={styles.ready}>
								<Icon name="check" size={14} /> World ready · suitable for colonization
							</span>
						)}
					</div>

					{phase === "done" ? (
						<div className={styles.footActions}>
							<Button variant="ghost" icon="refresh" onClick={generate}>
								Regenerate
							</Button>
							<Button variant="primary" stencil iconRight="arrowRight" onClick={() => go("landing")}>
								Accept World
							</Button>
						</div>
					) : (
						<Button variant="primary" stencil icon="globe" onClick={generate} disabled={phase === "generating"}>
							{phase === "generating" ? "Generating…" : "Generate"}
						</Button>
					)}
				</footer>
			</div>
		</div>
	);
}
