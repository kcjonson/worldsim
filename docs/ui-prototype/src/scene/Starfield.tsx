import { useMemo } from "react";
import styles from "./Starfield.module.css";

function mulberry32(seed: number) {
	return function () {
		seed |= 0;
		seed = (seed + 0x6d2b79f5) | 0;
		let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
		t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
		return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
	};
}

interface Star {
	x: number;
	y: number;
	r: number;
	o: number;
}

function makeStars(seed: number, count: number, maxR: number): Star[] {
	const rng = mulberry32(seed);
	return Array.from({ length: count }, () => ({
		x: rng() * 100,
		y: rng() * 100,
		r: 0.04 + rng() * maxR,
		o: 0.25 + rng() * 0.75,
	}));
}

export interface StarfieldProps {
	seed?: number;
	/* dim the field so foreground content reads */
	dim?: boolean;
	className?: string;
}

export function Starfield({ seed = 7, dim, className }: StarfieldProps) {
	const far = useMemo(() => makeStars(seed, 220, 0.12), [seed]);
	const near = useMemo(() => makeStars(seed + 99, 55, 0.2), [seed]);

	return (
		<div className={[styles.field, dim ? styles.dim : "", className ?? ""].filter(Boolean).join(" ")} aria-hidden="true">
			<div className={styles.nebula} />
			<svg className={styles.layerFar} viewBox="0 0 100 100" preserveAspectRatio="xMidYMid slice">
				{far.map((s, i) => (
					<circle key={i} cx={s.x} cy={s.y} r={s.r} fill="#cdd6e6" opacity={s.o} />
				))}
			</svg>
			<svg className={styles.layerNear} viewBox="0 0 100 100" preserveAspectRatio="xMidYMid slice">
				{near.map((s, i) => (
					<circle key={i} cx={s.x} cy={s.y} r={s.r} fill="#ffffff" opacity={s.o} />
				))}
			</svg>
			<div className={`${styles.vignette} fx-grain`} />
		</div>
	);
}
