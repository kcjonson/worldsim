import { useState } from "react";
import { Button, Panel, Meter, Badge, Avatar, Icon, Tabs, KeyCap, type IconName, type TabItem } from "../../design-system";
import {
	GAME_COLONISTS,
	RESOURCES,
	TASKS,
	TASK_TOTAL,
	NOTIFICATIONS,
	CARRIED,
	HELD,
	BELT_SLOTS,
	CARRY_CAP_KG,
	stackKg,
	totalCarryKg,
	fitTag,
	OFFMAP_COLONISTS,
} from "../../data/mock";
import type { ScreenProps } from "../types";
import { ColonistDetailsDialog } from "./ColonistDetailsDialog";
import styles from "./InGame.module.css";

type Speed = "pause" | "play" | "fast" | "vfast";

interface CommandCat {
	id: string;
	label: string;
	icon: IconName;
	items: { label: string; icon: IconName }[];
}
const COMMANDS: CommandCat[] = [
	{ id: "orders", label: "Orders", icon: "bolt", items: [
		{ label: "Mine", icon: "mountain" }, { label: "Chop Wood", icon: "leaf" }, { label: "Harvest", icon: "sprout" }, { label: "Haul", icon: "box" }, { label: "Cancel", icon: "close" } ] },
	{ id: "zone", label: "Zones", icon: "layers", items: [
		{ label: "Stockpile", icon: "box" }, { label: "Growing", icon: "sprout" }, { label: "Dumping", icon: "minus" } ] },
	{ id: "build", label: "Build", icon: "hammer", items: [
		{ label: "Foundation", icon: "home" }, { label: "Wall", icon: "minus" }, { label: "Door", icon: "arrowRight" }, { label: "Floor", icon: "layers" } ] },
	{ id: "make", label: "Production", icon: "box", items: [
		{ label: "Campfire", icon: "energy" }, { label: "Crafting Spot", icon: "hammer" }, { label: "Shelf", icon: "box" } ] },
];

const INFO_TABS: TabItem<string>[] = [
	{ value: "needs", label: "Needs", icon: "heart" },
	{ value: "bio", label: "Bio", icon: "user" },
	{ value: "gear", label: "Gear", icon: "box" },
	{ value: "log", label: "Log", icon: "list" },
];

const SPEEDS: { id: Speed; icon: IconName }[] = [
	{ id: "pause", icon: "pause" },
	{ id: "play", icon: "play" },
	{ id: "fast", icon: "fast" },
	{ id: "vfast", icon: "veryFast" },
];

/* Project an off-map bearing (0 = north, clockwise) onto the minimap's inset perimeter. */
function edgePoint(bearingDeg: number): { x: number; y: number } {
	const a = (bearingDeg * Math.PI) / 180;
	const dx = Math.sin(a);
	const dy = -Math.cos(a);
	const half = 0.5 - 0.12;
	const scale = half / Math.max(Math.abs(dx), Math.abs(dy));
	return { x: (0.5 + dx * scale) * 100, y: (0.5 + dy * scale) * 100 };
}

function moodColor(m: number): string {
	return m < 0.3 ? "var(--status-crit)" : m < 0.55 ? "var(--status-warn)" : "var(--status-ok)";
}

export function InGame({ go }: ScreenProps) {
	const [speed, setSpeed] = useState<Speed>("play");
	const [selectedId, setSelectedId] = useState(GAME_COLONISTS[0].id);
	const [storageOpen, setStorageOpen] = useState(true);
	const [tasksOpen, setTasksOpen] = useState(true);
	const [openMenu, setOpenMenu] = useState<string | null>("build");
	const [tab, setTab] = useState("needs");
	const [dismissed, setDismissed] = useState<number[]>([]);
	const [detailsOpen, setDetailsOpen] = useState(false);

	const selected = GAME_COLONISTS.find((c) => c.id === selectedId)!;
	const toasts = NOTIFICATIONS.filter((_, i) => !dismissed.includes(i));
	const carriedKg = totalCarryKg();
	const armed = HELD.twoHanded ?? HELD.left ?? HELD.right;

	return (
		<div className={styles.screen}>
			{/* faux 2D game world */}
			<div className={styles.world}>
				<div className={styles.terrain} />
				<div className={styles.grid} />
				<div className={styles.river} />
				{/* scattered entities */}
				<span className={styles.tree} style={{ top: "32%", left: "24%" }} />
				<span className={styles.tree} style={{ top: "60%", left: "18%" }} />
				<span className={styles.tree} style={{ top: "44%", left: "70%" }} />
				<span className={styles.bush} style={{ top: "70%", left: "58%" }} />
				<span className={styles.bush} style={{ top: "38%", left: "52%" }} />
				{/* crashed ship */}
				<div className={styles.ship}>
					<Icon name="rocket" size={26} />
				</div>
				{/* selected colonist on the map */}
				<div className={styles.selection}>
					<span className={styles.colonistDot} />
					<span className={styles.selRing} />
					<span className={styles.selLabel}>{selected.name.split(" ")[0]}</span>
				</div>
				<div className={styles.vignette} />
			</div>

			{/* ===== TOP BAR ===== */}
			<header className={styles.topbar}>
				<div className={styles.topLeft}>
					<div className={styles.colony}>
						<span className={styles.colonyMark}>◈</span>
						<div>
							<div className={styles.colonyName}>Hollow Reach</div>
							<div className={styles.colonySub}>3 survivors · Sol 14</div>
						</div>
					</div>
				</div>

				<div className={styles.topCenter}>
					<div className={styles.clock}>
						<span className={styles.day}>Day 14</span>
						<Badge tone="data">Late Spring</Badge>
						<span className={styles.time}>09:42</span>
					</div>
					<div className={styles.speeds}>
						{SPEEDS.map((s) => (
							<button
								key={s.id}
								className={[styles.speedBtn, speed === s.id ? styles.speedActive : ""].filter(Boolean).join(" ")}
								onClick={() => setSpeed(s.id)}
							>
								<Icon name={s.icon} size={14} />
							</button>
						))}
					</div>
				</div>

				<div className={styles.topRight}>
					<button className={styles.alertBtn}>
						<Icon name="alert" size={16} />
						<span className={styles.alertCount}>1</span>
					</button>
					<Button variant="secondary" size="sm" icon="menu" onClick={() => go("menu")}>
						Menu
					</Button>
				</div>
			</header>

			{/* ===== COLONIST ROSTER (left) ===== */}
			<div className={styles.roster}>
				{GAME_COLONISTS.map((c) => (
					<button
						key={c.id}
						className={[styles.rosterCard, c.id === selectedId ? styles.rosterActive : ""].filter(Boolean).join(" ")}
						onClick={() => setSelectedId(c.id)}
						onDoubleClick={() => {
							setSelectedId(c.id);
							setDetailsOpen(true);
						}}
						title="Double-click for full dossier"
					>
						<Avatar seed={c.name} mood={c.mood} size={30} selected={c.id === selectedId} />
						<div className={styles.rosterInfo}>
							<div className={styles.rosterRow}>
								<span className={styles.rosterName}>{c.name.split(" ")[0]}</span>
								<span className={styles.rosterMood} style={{ color: moodColor(c.mood) }}>
									{Math.round(c.mood * 100)}%
								</span>
							</div>
							<Meter
								inline
								label={c.task}
								valueText={`${Math.round(c.taskProgress * 100)}%`}
								value={c.taskProgress}
								tone="accent"
								size="sm"
							/>
						</div>
					</button>
				))}
			</div>

			{/* ===== SELECTED ENTITY INFO (bottom-left) ===== */}
			<div className={styles.infoPanel}>
				<Panel accent="accent" corners flush>
					<div className={styles.infoHead}>
						<Avatar seed={selected.name} mood={selected.mood} size={52} />
						<div className={styles.infoHeadText}>
							<div className={styles.infoName}>{selected.name}</div>
							<Meter inline label="Mood" value={selected.mood} valueText={`${Math.round(selected.mood * 100)}%`} tone="auto" />
							<Meter inline label={selected.task} value={selected.taskProgress} valueText={`${Math.round(selected.taskProgress * 100)}%`} tone="accent" />
						</div>
						<div className={styles.infoHeadBtns}>
							<button className={styles.infoClose} onClick={() => setDetailsOpen(true)} title="Open full dossier">
								<Icon name="eye" size={14} />
							</button>
							<button className={styles.infoClose}>
								<Icon name="close" size={14} />
							</button>
						</div>
					</div>

					<Tabs tabs={INFO_TABS} value={tab} onChange={setTab} className={styles.infoTabs} />

					<div className={styles.infoBody}>
						{tab === "needs" && (
							<div className={styles.needs}>
								{selected.needs.map((n) => (
									<div key={n.name} className={styles.needRow}>
										<Icon name={n.icon} size={13} className={styles.needIcon} />
										<Meter label={n.name} value={n.value} valueText={`${Math.round(n.value * 100)}%`} tone="auto" size="sm" />
									</div>
								))}
							</div>
						)}
						{tab === "bio" && (
							<p className={styles.bioText}>
								Flight engineer, 34. Steady hands, sleeps poorly. Took the expedition to outrun a debt she won't discuss.
							</p>
						)}
						{tab === "gear" && (
							<div className={styles.gearQuick}>
								<div className={styles.gearArmed}>
									<Icon name={armed ? armed.icon : "minus"} size={12} />
									{armed ? armed.name : "Unarmed"}
									{armed && (
										<span className={styles.gearHint}>
											· {fitTag(armed.hands)} · {HELD.twoHanded ? "both hands" : "one hand"}
										</span>
									)}
								</div>
								{BELT_SLOTS.some(Boolean) && (
									<div className={styles.gearBelt}>
										<span className={styles.gearBeltLabel}>Belt</span>
										{BELT_SLOTS.map(
											(it, i) =>
												it && (
													<span key={i} className={styles.invChip} title={`${it.name} · one-hand`}>
														<Icon name={it.icon} size={12} />
													</span>
												),
										)}
									</div>
								)}
								<Meter
									label="Carry"
									value={carriedKg / CARRY_CAP_KG}
									valueText={`${carriedKg.toFixed(1)} / ${CARRY_CAP_KG} kg`}
									tone="data"
									size="sm"
								/>
								<div className={styles.invChips}>
									{CARRIED.map((s) => (
										<span key={s.name} className={styles.invChip} title={`${s.name} ×${s.qty} · ${stackKg(s).toFixed(1)} kg`}>
											<Icon name={s.icon} size={12} />
											{s.qty > 1 && <span className={styles.invQty}>{s.qty}</span>}
										</span>
									))}
								</div>
							</div>
						)}
						{tab === "log" && (
							<div className={styles.log}>
								<div>09:40 — Started Foundation in Sector 4</div>
								<div>09:12 — Hauled Wood ×4 to Stockpile A</div>
								<div>08:30 — Ate a simple meal</div>
							</div>
						)}
					</div>

					<div className={styles.infoActions}>
						<Button variant="data" size="sm" icon="bolt">Draft</Button>
						<Button variant="secondary" size="sm" icon="crosshair">Go to</Button>
						<Button variant="ghost" size="sm" icon="list">Priorities</Button>
					</div>
				</Panel>
			</div>

			{/* ===== RIGHT STACK: minimap + resources + tasks ===== */}
			<div className={styles.rightStack}>
				<Panel title="Region" accent="data" corners compact flush bodyClassName={styles.minimapBody}>
					<svg className={styles.minimap} viewBox="0 0 100 64" preserveAspectRatio="xMidYMid slice">
						<rect width="100" height="64" fill="#0c130e" />
						<ellipse cx="34" cy="34" rx="40" ry="30" fill="#2c3a26" />
						<ellipse cx="22" cy="52" rx="22" ry="14" fill="#34431f" />
						<path d="M68 -4 L84 -4 L96 30 L96 68 L60 68 L52 34 Z" fill="#16323f" opacity="0.9" />
						<path d="M58 0 L66 26 L60 64" stroke="#245063" strokeWidth="3" fill="none" opacity="0.7" />
						<g opacity="0.22" stroke="#7fa0c0" strokeWidth="0.4">
							<path d="M25 0V64M50 0V64M75 0V64M0 21H100M0 43H100" />
						</g>
						<rect x="42" y="30" width="5" height="5" fill="#e8a33e" transform="rotate(-24 44.5 32.5)" />
						<circle cx="30" cy="22" r="1.3" fill="#5fb87a" />
						<circle cx="20" cy="48" r="1.3" fill="#5fb87a" />
						<circle cx="58" cy="44" r="1.3" fill="#5fb87a" />
						<circle cx="46" cy="33" r="1.6" fill="#f4eee2" />
						<circle cx="49" cy="36" r="1.6" fill="#f4eee2" />
						<circle cx="43" cy="38" r="1.6" fill="#f4eee2" />
						<rect x="36" y="22" width="26" height="20" fill="none" stroke="#ffc56b" strokeWidth="0.8" />
					</svg>
					<span className={styles.minimapCoords}>14.2°N · 9.8°W</span>
					{OFFMAP_COLONISTS.map((m) => {
						const p = edgePoint(m.bearing);
						return (
							<div
								key={m.name}
								className={styles.offmark}
								style={{ left: `${p.x}%`, top: `${p.y}%` }}
								title={`${m.name} · ${m.dist} this way`}
							>
								<Icon name="chevronUp" size={12} className={styles.offArrow} style={{ transform: `rotate(${m.bearing}deg)` }} />
								<span className={styles.offDist}>{m.dist}</span>
							</div>
						);
					})}
				</Panel>

				<Panel
					title="Storage"
					accent="data"
					corners
					compact
					actions={
						<button className={styles.collapse} onClick={() => setStorageOpen((s) => !s)}>
							<Icon name={storageOpen ? "chevronUp" : "chevronDown"} size={14} />
						</button>
					}
				>
					{storageOpen && (
						<div className={styles.resList}>
							{RESOURCES.map((r) => (
								<div key={r.name} className={styles.resRow}>
									<Icon name={r.icon} size={14} className={styles.resIcon} />
									<span className={styles.resName}>{r.name}</span>
									<span className={styles.resCount}>{r.count}</span>
								</div>
							))}
						</div>
					)}
				</Panel>

				<Panel
					title="Tasks"
					kicker={`${TASK_TOTAL.toLocaleString()} queued`}
					corners
					compact
					actions={
						<button className={styles.collapse} onClick={() => setTasksOpen((s) => !s)}>
							<Icon name={tasksOpen ? "chevronUp" : "chevronDown"} size={14} />
						</button>
					}
				>
					{tasksOpen && (
						<div className={styles.taskList}>
							{TASKS.map((t, i) => (
								<div key={i} className={styles.taskRow}>
									<span className={[styles.taskDot, styles[`s_${t.status}`]].join(" ")} />
									<div className={styles.taskMain}>
										<span className={styles.taskLabel}>{t.label}</span>
										<span className={styles.taskDetail}>{t.detail}</span>
									</div>
									{t.colonist && <span className={styles.taskWho}>{t.colonist}</span>}
								</div>
							))}
							<div className={styles.taskMore}>+{(TASK_TOTAL - TASKS.length).toLocaleString()} more · filter to refine</div>
						</div>
					)}
				</Panel>
			</div>

			{/* ===== ZOOM CONTROLS ===== */}
			<div className={styles.zoom}>
				<button className={styles.zoomBtn}><Icon name="plus" size={16} /></button>
				<button className={styles.zoomBtn}><Icon name="home" size={15} /></button>
				<button className={styles.zoomBtn}><Icon name="minus" size={16} /></button>
			</div>

			{/* ===== TOASTS ===== */}
			<div className={styles.toasts}>
				{toasts.map((n) => {
					const realIndex = NOTIFICATIONS.indexOf(n);
					return (
						<div key={realIndex} className={[styles.toast, styles[`t_${n.severity}`]].join(" ")}>
							<Icon name={n.severity === "crit" ? "alert" : n.severity === "warn" ? "alert" : "info"} size={16} className={styles.toastIcon} />
							<div className={styles.toastText}>
								<div className={styles.toastTitle}>{n.title}</div>
								<div className={styles.toastBody}>{n.body}</div>
							</div>
							<button className={styles.toastClose} onClick={() => setDismissed((d) => [...d, realIndex])}>
								<Icon name="close" size={12} />
							</button>
						</div>
					);
				})}
			</div>

			{/* ===== COMMAND BAR (bottom center) ===== */}
			<div className={styles.commandBar}>
				{COMMANDS.map((cat) => (
					<div key={cat.id} className={styles.cmdWrap}>
						{openMenu === cat.id && (
							<div className={styles.cmdMenu}>
								{cat.items.map((it) => (
									<button key={it.label} className={styles.cmdItem}>
										<Icon name={it.icon} size={14} />
										{it.label}
									</button>
								))}
							</div>
						)}
						<button
							className={[styles.cmdBtn, openMenu === cat.id ? styles.cmdActive : ""].filter(Boolean).join(" ")}
							onClick={() => setOpenMenu((m) => (m === cat.id ? null : cat.id))}
						>
							<Icon name={cat.icon} size={16} />
							{cat.label}
							<Icon name="chevronUp" size={12} className={styles.cmdCaret} />
						</button>
					</div>
				))}
				<div className={styles.cmdHint}>
					<KeyCap>B</KeyCap> build · <KeyCap>Space</KeyCap> pause
				</div>
			</div>

			<ColonistDetailsDialog open={detailsOpen} onClose={() => setDetailsOpen(false)} colonist={selected} />
		</div>
	);
}
