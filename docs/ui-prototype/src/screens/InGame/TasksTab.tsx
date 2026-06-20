import { Meter, Badge, Icon, Divider } from "../../design-system";
import { CURRENT_TASK, KNOWN_TASKS, taskStateMeta, type KnownTask } from "../../data/mock";
import styles from "./TasksTab.module.css";

function TaskRow({ t }: { t: KnownTask }) {
	const meta = taskStateMeta(t.state);
	return (
		<div className={styles.row}>
			<Icon name={t.icon} size={14} className={styles.rowIcon} />
			<div className={styles.rowMain}>
				<span className={styles.rowLabel}>{t.label}</span>
				<span className={styles.rowDetail}>{t.detail}</span>
			</div>
			<span className={styles.rowDist}>{t.dist}</span>
			<Badge tone={meta.tone}>{meta.label}</Badge>
		</div>
	);
}

export function TasksTab() {
	const c = CURRENT_TASK;
	return (
		<div className={styles.tasks}>
			<div className={styles.current}>
				<div className={styles.curKicker}>Currently</div>
				<div className={styles.curHead}>
					<Icon name={c.icon} size={16} className={styles.curIcon} />
					<span className={styles.curType}>{c.type}</span>
					<span className={styles.curLabel}>{c.label}</span>
					<span className={styles.curNav}>
						{c.nav} · {c.dist}
					</span>
				</div>
				<Meter value={c.progress} valueText={`${Math.round(c.progress * 100)}%`} tone="accent" size="sm" />
			</div>

			<Divider label="Known Work" />
			<div className={styles.list}>
				{KNOWN_TASKS.map((t, i) => (
					<TaskRow key={i} t={t} />
				))}
			</div>
			<p className={styles.muted}>
				Colonists pick the highest-priority job they know about, on their own. Per-colonist preferences live under
				Work Priorities.
			</p>
		</div>
	);
}
