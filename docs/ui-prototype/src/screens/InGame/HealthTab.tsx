import { Meter, Divider, Icon } from "../../design-system";
import { DOSSIER_NEEDS, DOSSIER_MOOD, moodLabel, needTone, type ColonistNeed } from "../../data/mock";
import styles from "./HealthTab.module.css";

function NeedRow({ n }: { n: ColonistNeed }) {
	return (
		<div className={[styles.needRow, n.tier === "comfort" ? styles.comfort : ""].filter(Boolean).join(" ")}>
			<Icon name={n.icon} size={14} className={styles.needIcon} />
			<Meter
				label={n.name}
				value={n.value}
				valueText={`${Math.round(n.value * 100)}%`}
				tone={needTone(n)}
				size="sm"
				className={styles.needMeter}
			/>
		</div>
	);
}

export function HealthTab() {
	const vital = DOSSIER_NEEDS.filter((n) => n.tier === "vital");
	const comfort = DOSSIER_NEEDS.filter((n) => n.tier === "comfort");
	const mood = DOSSIER_MOOD;

	return (
		<div className={styles.health}>
			<Meter label="Mood" value={mood} valueText={`${Math.round(mood * 100)}% · ${moodLabel(mood)}`} tone="auto" />
			<p className={styles.derived}>Mood is computed from the needs below; it sinks as they go unmet.</p>

			<div className={styles.cols}>
				<div className={styles.col}>
					<Divider label="Vital Needs" />
					<div className={styles.needs}>
						{vital.map((n) => (
							<NeedRow key={n.name} n={n} />
						))}
					</div>
					<Divider label="Comfort" />
					<div className={styles.needs}>
						{comfort.map((n) => (
							<NeedRow key={n.name} n={n} />
						))}
					</div>
					<p className={styles.muted}>Comfort needs are tracked, but colonists don't act on them yet.</p>
				</div>

				<div className={styles.col}>
					<Divider label="Body & Ailments" />
					<div className={styles.empty}>
						<Icon name="heart" size={16} className={styles.emptyIcon} />
						<div>
							<div className={styles.emptyTitle}>No injuries or ailments</div>
							<div className={styles.emptySub}>Wounds, illness, and treatment arrive with the medical update.</div>
						</div>
					</div>
				</div>
			</div>
		</div>
	);
}
