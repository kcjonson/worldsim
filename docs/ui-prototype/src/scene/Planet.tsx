import { useMemo } from "react";
import styles from "./Planet.module.css";

export type PlanetMode = "terrain" | "biome" | "temp" | "precip";

interface Blob {
	u: number; // 0..1 longitude
	v: number; // -1..1 latitude
	r: number;
	hue: number;
}

function mulberry32(seed: number) {
	return function () {
		seed |= 0;
		seed = (seed + 0x6d2b79f5) | 0;
		let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
		t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
		return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
	};
}

function makeContinents(seed: number): Blob[] {
	const rng = mulberry32(seed);
	const blobs: Blob[] = [];
	const continents = 4 + Math.floor(rng() * 3);
	for (let c = 0; c < continents; c++) {
		const cu = rng();
		const cv = (rng() - 0.5) * 1.5;
		const lobes = 4 + Math.floor(rng() * 5);
		for (let l = 0; l < lobes; l++) {
			blobs.push({
				u: (cu + (rng() - 0.5) * 0.12) % 1,
				v: Math.max(-0.95, Math.min(0.95, cv + (rng() - 0.5) * 0.35)),
				r: 8 + rng() * 16,
				hue: rng(),
			});
		}
	}
	return blobs;
}

function landColor(mode: PlanetMode, hue: number): string {
	switch (mode) {
		case "terrain": {
			// lowland green -> highland tan -> peak
			const t = hue;
			if (t < 0.5) return "#3f6b3a";
			if (t < 0.8) return "#6e7a3c";
			return "#9a8a5c";
		}
		case "biome": {
			const palette = ["#2f6b46", "#4f7a32", "#8a8f3a", "#b39a4e", "#6a8f6a", "#3a6b66", "#9a6a3a"];
			return palette[Math.floor(hue * palette.length)];
		}
		case "temp":
			return "#5a6b4a";
		case "precip":
			return "#4a6b5a";
	}
}

export interface PlanetProps {
	mode?: PlanetMode;
	seed?: number;
	size?: number;
	rotate?: boolean;
	scan?: boolean;
	progress?: number; // 0..1
	marker?: { x: number; y: number } | null; // 0..1 within the disk box
	onPick?: (p: { x: number; y: number }) => void;
	className?: string;
}

export function Planet({
	mode = "terrain",
	seed = 42,
	size = 420,
	rotate = true,
	scan = false,
	progress = 1,
	marker,
	onPick,
	className,
}: PlanetProps) {
	const blobs = useMemo(() => makeContinents(seed), [seed]);
	const clipId = `clip${seed}`;
	const oceanId = `ocean${seed}`;
	const shadeId = `shade${seed}`;
	const landReveal = Math.max(0, Math.min(1, (progress - 0.15) / 0.6));

	const renderBand = (offset: number) =>
		blobs.map((b, i) => (
			<circle
				key={`${offset}-${i}`}
				cx={b.u * 200 + offset}
				cy={100 + b.v * 78}
				r={b.r}
				fill={landColor(mode, b.hue)}
				opacity={0.92}
			/>
		));

	const handleClick = (e: React.MouseEvent<SVGRectElement>) => {
		if (!onPick) return;
		const rect = e.currentTarget.getBoundingClientRect();
		const x = (e.clientX - rect.left) / rect.width;
		const y = (e.clientY - rect.top) / rect.height;
		onPick({ x, y });
	};

	return (
		<div className={[styles.wrap, className ?? ""].filter(Boolean).join(" ")} style={{ width: size, height: size }}>
			<svg viewBox="0 0 200 200" width="100%" height="100%" className={styles.svg}>
				<defs>
					<clipPath id={clipId}>
						<circle cx="100" cy="100" r="92" />
					</clipPath>
					<radialGradient id={oceanId} cx="38%" cy="32%" r="75%">
						<stop offset="0%" stopColor="#1c4a6e" />
						<stop offset="60%" stopColor="#123249" />
						<stop offset="100%" stopColor="#0a1c2c" />
					</radialGradient>
					<radialGradient id={shadeId} cx="35%" cy="30%" r="80%">
						<stop offset="0%" stopColor="rgba(255,255,255,0.22)" />
						<stop offset="45%" stopColor="rgba(255,255,255,0)" />
						<stop offset="100%" stopColor="rgba(0,0,0,0.7)" />
					</radialGradient>
					<linearGradient id={`temp${seed}`} x1="0" y1="0" x2="0" y2="1">
						<stop offset="0%" stopColor="#2a5b9a" />
						<stop offset="50%" stopColor="#c44a2e" />
						<stop offset="100%" stopColor="#2a5b9a" />
					</linearGradient>
					<linearGradient id={`precip${seed}`} x1="0" y1="0" x2="0" y2="1">
						<stop offset="0%" stopColor="#3a6b7a" />
						<stop offset="22%" stopColor="#8a6a3a" />
						<stop offset="50%" stopColor="#3f7a4a" />
						<stop offset="78%" stopColor="#8a6a3a" />
						<stop offset="100%" stopColor="#3a6b7a" />
					</linearGradient>
				</defs>

				{/* atmosphere halo */}
				<circle cx="100" cy="100" r="97" className={styles.atmo} />

				{/* ocean base */}
				<circle cx="100" cy="100" r="92" fill={`url(#${oceanId})`} />

				{/* continents (rotating band, seamless duplicate) */}
				<g clipPath={`url(#${clipId})`} opacity={landReveal}>
					<g className={rotate ? styles.spin : ""}>
						{renderBand(0)}
						{renderBand(200)}
					</g>
				</g>

				{/* climate overlays */}
				{mode === "temp" && (
					<rect
						x="8"
						y="8"
						width="184"
						height="184"
						clipPath={`url(#${clipId})`}
						fill={`url(#temp${seed})`}
						className={styles.tempOverlay}
					/>
				)}
				{mode === "precip" && (
					<rect
						x="8"
						y="8"
						width="184"
						height="184"
						clipPath={`url(#${clipId})`}
						fill={`url(#precip${seed})`}
						className={styles.precipOverlay}
					/>
				)}

				{/* graticule */}
				<g clipPath={`url(#${clipId})`} className={styles.grat}>
					{[-60, -30, 0, 30, 60].map((lat) => {
						const y = 100 + (lat / 90) * 86;
						const rx = 92 * Math.cos((lat / 90) * (Math.PI / 2));
						return <ellipse key={lat} cx="100" cy={y} rx={rx} ry={3} />;
					})}
					{[0, 30, 60, 90, 120, 150].map((lon) => {
						const rx = 92 * Math.abs(Math.cos((lon / 180) * Math.PI));
						return <ellipse key={lon} cx="100" cy="100" rx={Math.max(2, rx)} ry={92} />;
					})}
				</g>

				{/* sphere shading */}
				<circle cx="100" cy="100" r="92" fill={`url(#${shadeId})`} pointerEvents="none" />

				{/* scan sweep */}
				{scan && (
					<g clipPath={`url(#${clipId})`}>
						<rect x="8" width="184" height="3" className={styles.sweep} />
					</g>
				)}

				{/* rim */}
				<circle cx="100" cy="100" r="92" className={styles.rim} />

				{/* landing marker */}
				{marker && (
					<g transform={`translate(${marker.x * 200}, ${marker.y * 200})`} className={styles.marker}>
						<circle r="9" className={styles.markerRing} />
						<circle r="2.5" className={styles.markerDot} />
						<path d="M0 -13V-6M0 13V6M-13 0H-6M13 0H6" className={styles.markerTicks} />
					</g>
				)}

				{/* click target */}
				{onPick && (
					<rect x="8" y="8" width="184" height="184" fill="transparent" clipPath={`url(#${clipId})`} onClick={handleClick} className={styles.hit} />
				)}
			</svg>
		</div>
	);
}
