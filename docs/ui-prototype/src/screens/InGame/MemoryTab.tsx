import { Icon, Badge } from "../../design-system";
import { MEMORY_CATEGORIES, MEMORY_TOTAL, DOSSIER_SIGHT_M, type MemoryCategory } from "../../data/mock";
import styles from "./MemoryTab.module.css";

function Category({ c }: { c: MemoryCategory }) {
	return (
		<div className={styles.cat}>
			<div className={styles.catHead}>
				<Icon name={c.icon} size={14} className={styles.catIcon} />
				<span className={styles.catName}>{c.name}</span>
				<Badge tone={c.count === 0 ? "default" : c.tone}>{c.count}</Badge>
			</div>
			{c.things.length === 0 ? (
				<div className={styles.none}>None sighted</div>
			) : (
				<div className={styles.things}>
					{c.things.map((t, i) => (
						<div key={i} className={styles.thing}>
							<span className={styles.thingName}>{t.name}</span>
							<span className={styles.thingDist}>{t.dist}</span>
							<span className={[styles.thingSeen, t.stale ? styles.stale : ""].filter(Boolean).join(" ")}>
								{t.stale ? "may be gone" : t.seen}
							</span>
						</div>
					))}
					{c.count > c.things.length && <div className={styles.more}>+{c.count - c.things.length} more</div>}
				</div>
			)}
		</div>
	);
}

export function MemoryTab() {
	return (
		<div className={styles.memory}>
			<div className={styles.summary}>
				<div className={styles.metric}>
					<span className={styles.mVal}>{MEMORY_TOTAL}</span>
					<span className={styles.mLabel}>Locations known</span>
				</div>
				<div className={styles.metric}>
					<span className={styles.mVal}>{DOSSIER_SIGHT_M}m</span>
					<span className={styles.mLabel}>Sight range</span>
				</div>
			</div>

			<div className={styles.cats}>
				{MEMORY_CATEGORIES.map((c) => (
					<Category key={c.name} c={c} />
				))}
			</div>

			<p className={styles.muted}>
				Memory is personal — only what {`this colonist`} has seen, not the colony's shared map. Old sightings go
				stale until revisited.
			</p>
		</div>
	);
}
