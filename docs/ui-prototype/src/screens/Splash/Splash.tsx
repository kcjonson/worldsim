import { useEffect, useState } from "react";
import { Button } from "../../design-system";
import { Starfield } from "../../scene/Starfield";
import { BRAND } from "../../data/mock";
import type { ScreenProps } from "../types";
import styles from "./Splash.module.css";

const BOOT_LINES = [
	"NAV-CORE online",
	"Mounting salvage manifest",
	"Calibrating sensor grain",
	"Loading vector atlas",
	"Spinning up world cache",
	"Crew vitals nominal",
];

const FLAVOR = [
	"“The planet is earth-like. That's the good news.”",
	"“Rescue is eighteen years out. Best not to count.”",
	"“Everything you'll need is already here. It's just not yours yet.”",
	"“Other ships came down before you. Some were not human.”",
];

export function Splash({ go }: ScreenProps) {
	const [progress, setProgress] = useState(0);
	const [bootIndex, setBootIndex] = useState(0);
	const [flavor] = useState(() => FLAVOR[Math.floor(Math.random() * FLAVOR.length)]);

	useEffect(() => {
		const id = setInterval(() => {
			setProgress((p) => {
				const next = Math.min(100, p + 1.4 + Math.random() * 2.2);
				setBootIndex(Math.min(BOOT_LINES.length, Math.floor((next / 100) * BOOT_LINES.length) + 1));
				return next;
			});
		}, 60);
		return () => clearInterval(id);
	}, []);

	const done = progress >= 100;
	const phase = done ? "Ready" : BOOT_LINES[Math.min(BOOT_LINES.length - 1, bootIndex)] ?? "Booting";

	return (
		<div className={styles.screen}>
			<Starfield seed={3} />

			<div className={styles.center}>
				<div className={styles.mark}>◈</div>
				<h1 className={styles.title}>{BRAND.title}</h1>
				<div className={styles.tagline}>{BRAND.tagline}</div>
				<p className={styles.flavor}>{flavor}</p>
			</div>

			<div className={styles.bottom}>
				<div className={styles.bootlog}>
					{BOOT_LINES.slice(0, bootIndex).map((l, i) => (
						<div key={i} className={styles.bootline}>
							<span className={styles.ok}>[ OK ]</span> {l}
						</div>
					))}
				</div>

				{done ? (
					<Button variant="primary" size="lg" stencil icon="play" onClick={() => go("menu")} className={styles.enter}>
						Enter Expedition
					</Button>
				) : (
					<div className={styles.loader}>
						<div className={styles.loaderHead}>
							<span className={styles.phase}>{phase}</span>
							<span className={styles.pct}>{Math.floor(progress)}%</span>
						</div>
						<div className={styles.track}>
							<div className={styles.fill} style={{ width: `${progress}%` }} />
						</div>
					</div>
				)}
			</div>

			<div className={styles.version}>{BRAND.version}</div>
		</div>
	);
}
