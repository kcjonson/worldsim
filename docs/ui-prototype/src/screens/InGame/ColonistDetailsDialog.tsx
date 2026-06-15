import { useState } from "react";
import { Modal, Tabs, Avatar, Meter, Stat, Badge, Button, Icon, Divider, type TabItem } from "../../design-system";
import { COLONISTS, type GameColonist } from "../../data/mock";
import { GearTab } from "./GearTab";
import styles from "./ColonistDetailsDialog.module.css";

const TABS: TabItem<string>[] = [
	{ value: "bio", label: "Bio", icon: "user" },
	{ value: "needs", label: "Needs", icon: "heart" },
	{ value: "skills", label: "Skills", icon: "hammer" },
	{ value: "social", label: "Social", icon: "users" },
	{ value: "gear", label: "Gear", icon: "box" },
	{ value: "log", label: "Log", icon: "list" },
];

export interface ColonistDetailsDialogProps {
	open: boolean;
	onClose: () => void;
	colonist: GameColonist;
}

export function ColonistDetailsDialog({ open, onClose, colonist }: ColonistDetailsDialogProps) {
	const [tab, setTab] = useState("bio");
	const detail = COLONISTS.find((c) => c.name === colonist.name) ?? COLONISTS[0];

	return (
		<Modal
			open={open}
			onClose={onClose}
			size="lg"
			kicker={`Personnel File · ${detail.role}`}
			title={colonist.name}
			footer={
				<>
					<Button variant="ghost" size="sm" onClick={onClose}>Close</Button>
					<Button variant="secondary" size="sm" icon="list">Work Priorities</Button>
					<Button variant="primary" size="sm" icon="bolt">Draft</Button>
				</>
			}
		>
			<div className={styles.header}>
				<Avatar seed={colonist.name} mood={colonist.mood} size={72} />
				<div className={styles.headStats}>
					<Stat label="Role" value={detail.role} size="sm" />
					<Stat label="Origin" value={detail.origin} size="sm" />
					<Stat label="Age" value={detail.age} unit="yrs" size="sm" />
					<Stat label="Mood" value={`${Math.round(colonist.mood * 100)}%`} tone={colonist.mood < 0.4 ? "crit" : "ok"} size="sm" />
				</div>
			</div>

			<Tabs tabs={TABS} value={tab} onChange={setTab} className={styles.tabs} />

			<div className={styles.tabBody}>
				{tab === "bio" && (
					<>
						<p className={styles.bio}>{detail.backstory}</p>
						<Divider label="Traits" />
						<div className={styles.chips}>
							{detail.traits.map((t) => (
								<Badge key={t.name} tone={t.tone === "good" ? "ok" : t.tone === "bad" ? "crit" : "default"}>
									{t.name}
								</Badge>
							))}
						</div>
						<div className={styles.note}>
							<Icon name="hammer" size={13} /> Currently: {colonist.task}
						</div>
					</>
				)}

				{tab === "needs" && (
					<div className={styles.needs}>
						<Meter label="Mood" value={colonist.mood} valueText={`${Math.round(colonist.mood * 100)}%`} tone="auto" />
						<Divider />
						{colonist.needs.map((n) => (
							<div key={n.name} className={styles.needRow}>
								<Icon name={n.icon} size={14} className={styles.needIcon} />
								<Meter label={n.name} value={n.value} valueText={`${Math.round(n.value * 100)}%`} tone="auto" />
							</div>
						))}
					</div>
				)}

				{tab === "skills" && (
					<div className={styles.skills}>
						{detail.skills.map((s) => (
							<div key={s.name} className={styles.skillRow}>
								<Icon name={s.icon} size={14} className={styles.skillIcon} />
								<span className={styles.skillName}>{s.name}</span>
								<Meter value={s.level / 20} valueText={String(s.level)} tone="accent" size="sm" className={styles.skillMeter} />
							</div>
						))}
					</div>
				)}

				{tab === "social" && (
					<div className={styles.social}>
						<div className={styles.socialRow}>
							<Avatar seed="Idris Okonkwo" mood={0.58} size={36} />
							<div className={styles.socialMeta}>
								<div className={styles.socialName}>Idris Okonkwo</div>
								<div className={styles.socialRel}>Crewmate · Respected</div>
							</div>
							<Badge tone="ok">+24</Badge>
						</div>
						<div className={styles.socialRow}>
							<Avatar seed="Rin Calloway" mood={0.81} size={36} />
							<div className={styles.socialMeta}>
								<div className={styles.socialName}>Rin Calloway</div>
								<div className={styles.socialRel}>Crewmate · Acquaintance</div>
							</div>
							<Badge tone="default">+6</Badge>
						</div>
						<p className={styles.muted}>Relationships deepen as the colony shares meals, work, and close calls.</p>
					</div>
				)}

				{tab === "gear" && <GearTab />}

				{tab === "log" && (
					<div className={styles.log}>
						<div>09:40 — Started Foundation in Sector 4</div>
						<div>09:12 — Hauled Wood ×4 to Stockpile A</div>
						<div>08:30 — Ate a simple meal</div>
						<div>07:55 — Woke up (slept on the ground, −4 mood)</div>
						<div>06:10 — Finished repairing the salvage cutter</div>
					</div>
				)}
			</div>
		</Modal>
	);
}
