import type { IconName } from "../design-system";

export const BRAND = {
	title: "WORLD-SIM",
	tagline: "Prospecting Expedition 28-B",
	version: "v0.1.0-proto",
};

/* ===== Scenarios ===== */
export interface Scenario {
	id: string;
	name: string;
	blurb: string;
	difficulty: number; // 1..5
	party: string;
	icon: IconName;
	tags: string[];
}

export const SCENARIOS: Scenario[] = [
	{
		id: "standard",
		name: "Standard Colony",
		blurb: "A balanced wreck site with workable salvage and a temperate landing band. The recommended way in.",
		difficulty: 2,
		party: "3 survivors",
		icon: "home",
		tags: ["Balanced", "Recommended"],
	},
	{
		id: "harsh",
		name: "Harsh World",
		blurb: "Thin atmosphere, scarce water, a climate that does not negotiate. Salvage is light. For veterans.",
		difficulty: 4,
		party: "3 survivors",
		icon: "temp",
		tags: ["Scarcity", "Climate"],
	},
	{
		id: "rich",
		name: "Rich Resources",
		blurb: "Dense ore, lush flora, intact cargo pods scattered nearby. A forgiving economy to learn the ropes.",
		difficulty: 1,
		party: "4 survivors",
		icon: "box",
		tags: ["Abundant", "Casual"],
	},
	{
		id: "lone",
		name: "Lone Survivor",
		blurb: "One escape pod. One person. Everything else burned on re-entry. The hardest story we tell.",
		difficulty: 5,
		party: "1 survivor",
		icon: "user",
		tags: ["Solo", "Brutal"],
	},
	{
		id: "expedition",
		name: "Large Expedition",
		blurb: "A full survey crew rode the wreck down. More hands, more mouths, more politics. Sandbox-leaning.",
		difficulty: 3,
		party: "8 survivors",
		icon: "users",
		tags: ["Sandbox", "Management"],
	},
];

/* ===== Colonists ===== */
export interface Trait {
	name: string;
	tone: "good" | "bad" | "neutral";
}

export interface Skill {
	name: string;
	level: number; // 0..20
	icon: IconName;
}

export interface Colonist {
	id: string;
	name: string;
	role: string;
	origin: string;
	age: number;
	mood: number; // 0..1
	backstory: string;
	skills: Skill[];
	traits: Trait[];
}

export const COLONISTS: Colonist[] = [
	{
		id: "c1",
		name: "Mara Vance",
		role: "Flight Engineer",
		origin: "Outpost 28-B",
		age: 34,
		mood: 0.72,
		backstory:
			"Twelve years keeping prospecting rigs in the black. Knows which welds hold and which are lying to you. Took the expedition to outrun a debt she will not discuss.",
		skills: [
			{ name: "Construction", level: 14, icon: "hammer" },
			{ name: "Crafting", level: 11, icon: "box" },
			{ name: "Mining", level: 9, icon: "mountain" },
			{ name: "Medicine", level: 4, icon: "heart" },
			{ name: "Cooking", level: 6, icon: "food" },
			{ name: "Research", level: 8, icon: "search" },
		],
		traits: [
			{ name: "Steady Hands", tone: "good" },
			{ name: "Insomniac", tone: "bad" },
			{ name: "Hauler", tone: "neutral" },
		],
	},
	{
		id: "c2",
		name: "Idris Okonkwo",
		role: "Field Medic",
		origin: "Kessler Station",
		age: 41,
		mood: 0.58,
		backstory:
			"A trauma surgeon who left a good hospital for reasons that made sense at the time. Calm in a crisis, restless in the quiet between them.",
		skills: [
			{ name: "Medicine", level: 16, icon: "heart" },
			{ name: "Research", level: 12, icon: "search" },
			{ name: "Cooking", level: 9, icon: "food" },
			{ name: "Social", level: 11, icon: "users" },
			{ name: "Construction", level: 3, icon: "hammer" },
			{ name: "Mining", level: 2, icon: "mountain" },
		],
		traits: [
			{ name: "Compassionate", tone: "good" },
			{ name: "Squeamish (Mining)", tone: "bad" },
		],
	},
	{
		id: "c3",
		name: "Rin Calloway",
		role: "Botanist",
		origin: "Greenhouse Collective",
		age: 28,
		mood: 0.81,
		backstory:
			"Grew food in places food has no business growing. Talks to plants, and the unsettling part is that it seems to work. Optimist by stubbornness.",
		skills: [
			{ name: "Growing", level: 15, icon: "sprout" },
			{ name: "Cooking", level: 12, icon: "food" },
			{ name: "Research", level: 10, icon: "search" },
			{ name: "Animals", level: 8, icon: "leaf" },
			{ name: "Medicine", level: 6, icon: "heart" },
			{ name: "Construction", level: 5, icon: "hammer" },
		],
		traits: [
			{ name: "Green Thumb", tone: "good" },
			{ name: "Optimist", tone: "good" },
			{ name: "Frail", tone: "bad" },
		],
	},
];

/* extra crew shown when scenario party is larger */
export const RESERVE_COLONISTS: Colonist[] = [
	{
		id: "c4",
		name: "Dex Aludra",
		role: "Security",
		origin: "Belt Patrol",
		age: 37,
		mood: 0.49,
		backstory: "Carried a badge in three jurisdictions, kept it in none. Reliable when things go loud, prickly when they don't.",
		skills: [
			{ name: "Shooting", level: 14, icon: "bolt" },
			{ name: "Melee", level: 11, icon: "skull" },
			{ name: "Construction", level: 7, icon: "hammer" },
			{ name: "Mining", level: 9, icon: "mountain" },
			{ name: "Social", level: 4, icon: "users" },
			{ name: "Medicine", level: 5, icon: "heart" },
		],
		traits: [
			{ name: "Trigger-Steady", tone: "good" },
			{ name: "Abrasive", tone: "bad" },
		],
	},
];

/* ===== World gen ===== */
export interface WorldPreset {
	id: string;
	name: string;
	icon: IconName;
}
export const WORLD_PRESETS: WorldPreset[] = [
	{ id: "earthlike", name: "Earth-Like", icon: "globe" },
	{ id: "desert", name: "Desert World", icon: "temp" },
	{ id: "ocean", name: "Ocean World", icon: "water" },
	{ id: "frozen", name: "Frozen World", icon: "rain" },
	{ id: "volcanic", name: "Volcanic World", icon: "mountain" },
	{ id: "garden", name: "Ancient Garden", icon: "sprout" },
];

export const GEN_PHASES = [
	"Generating tectonic plates",
	"Simulating plate movement",
	"Raising terrain from collisions",
	"Modeling atmospheric circulation",
	"Calculating precipitation and rivers",
	"Forming oceans and seas",
	"Assigning biomes",
	"Calculating snow and glaciers",
	"Finalizing world data",
];

export interface WorldStat {
	label: string;
	value: string;
	tone?: "default" | "accent" | "data" | "ok" | "warn" | "crit";
}
export const WORLD_STATS: WorldStat[] = [
	{ label: "Land", value: "38%" },
	{ label: "Water", value: "62%", tone: "data" },
	{ label: "Continents", value: "4" },
	{ label: "Avg Temp", value: "14°C" },
	{ label: "Largest Sea", value: "Maed Belt" },
	{ label: "Volcanism", value: "Low", tone: "ok" },
];

export const CLIMATE_BREAKDOWN = [
	{ label: "Temperate", pct: 0.35 },
	{ label: "Tropical", pct: 0.2 },
	{ label: "Desert", pct: 0.2 },
	{ label: "Boreal", pct: 0.15 },
	{ label: "Polar", pct: 0.1 },
];

/* ===== Landing site ===== */
export interface LandingSite {
	biome: string;
	temp: string;
	rainfall: string;
	difficulty: number; // 1..5
	notes: string;
	hazards: string[];
}
export const SAMPLE_SITE: LandingSite = {
	biome: "Temperate Coastal Shelf",
	temp: "9°C – 22°C",
	rainfall: "Moderate",
	difficulty: 2,
	notes: "Flat alluvial ground beside a river mouth. Fresh water, soft soil, scattered hardwood analogues. A kind place to crash.",
	hazards: ["Seasonal storms", "Tidal flats"],
};

/* ===== In-game ===== */
export interface Need {
	name: string;
	value: number; // 0..1
	icon: IconName;
}
export interface GameColonist {
	id: string;
	name: string;
	mood: number;
	task: string;
	taskProgress: number; // 0..1
	needs: Need[];
}
export const GAME_COLONISTS: GameColonist[] = [
	{
		id: "g1",
		name: "Mara Vance",
		mood: 0.74,
		task: "Build Foundation",
		taskProgress: 0.62,
		needs: [
			{ name: "Food", value: 0.62, icon: "food" },
			{ name: "Rest", value: 0.4, icon: "rest" },
			{ name: "Water", value: 0.8, icon: "water" },
			{ name: "Recreation", value: 0.55, icon: "heart" },
		],
	},
	{
		id: "g2",
		name: "Idris Okonkwo",
		mood: 0.52,
		task: "Haul Wood ×6",
		taskProgress: 0.3,
		needs: [
			{ name: "Food", value: 0.3, icon: "food" },
			{ name: "Rest", value: 0.66, icon: "rest" },
			{ name: "Water", value: 0.71, icon: "water" },
			{ name: "Recreation", value: 0.28, icon: "heart" },
		],
	},
	{
		id: "g3",
		name: "Rin Calloway",
		mood: 0.83,
		task: "Harvest Reed",
		taskProgress: 0.48,
		needs: [
			{ name: "Food", value: 0.78, icon: "food" },
			{ name: "Rest", value: 0.59, icon: "rest" },
			{ name: "Water", value: 0.64, icon: "water" },
			{ name: "Recreation", value: 0.7, icon: "heart" },
		],
	},
];

export interface Resource {
	name: string;
	count: number;
	icon: IconName;
}
export const RESOURCES: Resource[] = [
	{ name: "Wood", count: 142, icon: "leaf" },
	{ name: "Stone", count: 88, icon: "mountain" },
	{ name: "Food", count: 36, icon: "food" },
	{ name: "Metal Scrap", count: 21, icon: "box" },
	{ name: "Plant Fiber", count: 54, icon: "sprout" },
];

export interface GameTask {
	label: string;
	detail: string;
	status: "active" | "pending" | "blocked";
	colonist?: string;
}
export const TASKS: GameTask[] = [
	{ label: "Build Foundation", detail: "Sector 4 · 62%", status: "active", colonist: "Mara" },
	{ label: "Haul Wood ×6", detail: "→ Stockpile A", status: "active", colonist: "Idris" },
	{ label: "Harvest Reed", detail: "Reed Cluster ×3", status: "active", colonist: "Rin" },
	{ label: "Cook Meals ×4", detail: "needs Campfire", status: "blocked" },
	{ label: "Mine Stone", detail: "Outcrop", status: "pending" },
	{ label: "Chop Oak ×3", detail: "North ridge", status: "pending" },
	{ label: "Haul Stone ×12", detail: "→ Stockpile A", status: "pending" },
	{ label: "Build Wall", detail: "Sector 4 · queued", status: "pending" },
	{ label: "Sow Field", detail: "Plot 2 · needs hoe", status: "blocked" },
	{ label: "Repair Cutter", detail: "needs Metal ×2", status: "blocked" },
	{ label: "Haul Berries ×8", detail: "→ Larder", status: "pending" },
	{ label: "Deconstruct Pod", detail: "Wreck · salvage", status: "pending" },
	{ label: "Tend Patient", detail: "Idris · minor", status: "active", colonist: "Rin" },
	{ label: "Haul Fiber ×20", detail: "→ Stockpile B", status: "pending" },
	{ label: "Dig Channel", detail: "River · queued", status: "pending" },
	{ label: "Cook Meals ×4", detail: "queued", status: "pending" },
];

/* total queued across the colony — most are off-screen; the panel shows priorities */
export const TASK_TOTAL = 1284;

export interface Notification {
	title: string;
	body: string;
	severity: "info" | "warn" | "crit";
}
export const NOTIFICATIONS: Notification[] = [
	{ title: "Construction complete", body: "Foundation in Sector 4 is finished.", severity: "info" },
	{ title: "Idris is hungry", body: "Food need is low. No prepared meals available.", severity: "warn" },
	{ title: "Storm front inbound", body: "Seasonal storm in ~2 days. Secure loose cargo.", severity: "crit" },
];

/* ===== Equipment ===== */
export interface GearItem {
	name: string;
	icon: IconName;
	qty?: number;
	hands?: 0 | 1 | 2;
	kg?: number;
}

/* Hand class = the engine's handsRequired (0 = pocket, 1 = one-hand, 2 = two-hand).
 * It gates WHERE an item can live: 2-hand items occupy both hands and can't be
 * stowed; pocket items fit a belt; one-hand fits hands or pack. */
export function fitLabel(hands: number): string {
	return hands === 2 ? "two-hand" : hands === 1 ? "one-hand" : "pocket";
}
export function fitTag(hands: number): string {
	return hands === 2 ? "2H" : hands === 1 ? "1H" : "•";
}

/* Worn + held slots. "col" places it on the left or right of the figure.
 * Body has two layers (under/over). Back and Belt hold containers that, when
 * equipped, expand into the pack/belt slot grids below. */
export interface WornSlot {
	key: string;
	label: string;
	col: "left" | "right";
	item: GearItem | null;
}
export const WORN_SLOTS: WornSlot[] = [
	{ key: "head", label: "Head", col: "left", item: { name: "Salvage Visor", icon: "eye" } },
	{ key: "face", label: "Face", col: "left", item: null },
	{ key: "over", label: "Body · Over", col: "left", item: { name: "Patched Duster", icon: "layers" } },
	{ key: "under", label: "Body · Under", col: "left", item: { name: "Thermal Weave", icon: "shirt" } },
	{ key: "legs", label: "Legs", col: "left", item: { name: "Canvas Trousers", icon: "pants" } },
	{ key: "feet", label: "Feet", col: "left", item: { name: "Worn Boots", icon: "boot" } },
	{ key: "back", label: "Back", col: "right", item: { name: "Field Pack", icon: "box" } },
	{ key: "belt", label: "Belt", col: "right", item: { name: "Tool Belt", icon: "box" } },
];

/* Held items. A two-handed item occupies BOTH hands and can't be stowed (the
 * engine's handsRequired === 2 rule). twoHanded set → ignore left/right. */
export interface HeldGear {
	name: string;
	icon: IconName;
	hands: 1 | 2;
	kg: number;
}
export const HELD: { twoHanded: HeldGear | null; left: HeldGear | null; right: HeldGear | null } = {
	twoHanded: { name: "Breaker Bar", icon: "hammer", hands: 2, kg: 4.0 },
	left: null,
	right: null,
};

/* Inventory is weight-based, not slot-based. Same-type items stack into one entry;
 * a stack's weight is qty × unitKg. The Back pack and the Belt add carry capacity
 * (in kg). "where" routes a stack to its container. */
export interface ItemStack {
	name: string;
	icon: IconName;
	qty: number;
	unitKg: number;
	hands: 0 | 1 | 2;
}
/* The pack (back container) is weight-based: any pocket or one-hand item, capped
 * by kilograms, not slot count. Same-type items stack. */
export const CARRIED: ItemStack[] = [
	{ name: "Wood", icon: "leaf", qty: 5, unitKg: 1.2, hands: 1 },
	{ name: "Metal Scrap", icon: "box", qty: 3, unitKg: 2.0, hands: 1 },
	{ name: "Field Ration", icon: "food", qty: 2, unitKg: 0.4, hands: 0 },
	{ name: "Plant Fiber", icon: "sprout", qty: 8, unitKg: 0.1, hands: 0 },
	{ name: "Bandage", icon: "heart", qty: 2, unitKg: 0.1, hands: 0 },
	{ name: "Stimpack", icon: "bolt", qty: 1, unitKg: 0.2, hands: 0 },
	{ name: "Signal Flare", icon: "energy", qty: 1, unitKg: 0.3, hands: 0 },
];

/* The belt is the exception to weight-based storage: discrete quick-draw SLOTS,
 * each holding exactly one ONE-HAND item (a tool or sidearm). Not pocket items,
 * not two-hand. Slot count comes from the equipped belt. The Hatchet here is the
 * tool the colonist stowed to free both hands for the two-handed Breaker Bar. */
export const BELT_SLOT_COUNT = 4;
export const BELT_SLOTS: (GearItem | null)[] = [
	{ name: "Hatchet", icon: "hammer", hands: 1, kg: 1.4 },
	{ name: "Multitool", icon: "gear", hands: 1, kg: 0.6 },
	null,
	null,
];

export const PACK_CAP_KG = 30; // kg the back container holds
export const CARRY_CAP_KG = 35; // total person carry weight (strength-derived)

export function stackKg(s: ItemStack): number {
	return Math.round(s.qty * s.unitKg * 10) / 10;
}
export function packKg(): number {
	return Math.round(CARRIED.reduce((sum, s) => sum + s.qty * s.unitKg, 0) * 10) / 10;
}
export function beltKg(): number {
	return Math.round(BELT_SLOTS.reduce((sum, s) => sum + (s?.kg ?? 0), 0) * 10) / 10;
}
export function heldKg(): number {
	return Math.round(((HELD.twoHanded?.kg ?? 0) + (HELD.left?.kg ?? 0) + (HELD.right?.kg ?? 0)) * 10) / 10;
}
/* total load the colonist is hauling: pack + belt + held (worn apparel excluded) */
export function totalCarryKg(): number {
	return Math.round((packKg() + beltKg() + heldKg()) * 10) / 10;
}

/* Colonists off the minimap's current view get an edge arrow + distance.
 * bearing: degrees, 0 = north, clockwise. */
export interface OffmapColonist {
	name: string;
	bearing: number;
	dist: string;
}
export const OFFMAP_COLONISTS: OffmapColonist[] = [
	{ name: "Dex", bearing: 40, dist: "180m" },
	{ name: "Joon", bearing: 118, dist: "95m" },
	{ name: "Vale", bearing: 318, dist: "240m" },
];

/* ===== Colonist dossier: Health / Memory / Tasks =====
 * These mirror the real game systems so the prototype drives a faithful in-game
 * dialog (see docs/design/game-systems digests):
 *  - Health is the Needs + Mood system. Eight needs (0..1), each with a "seek"
 *    threshold (below it the colonist goes to fulfill it) and a "critical"
 *    threshold (emergency). Mood is COMPUTED from the needs, not stored. Five
 *    needs are AI-actionable today; Hygiene, Recreation and Temperature are
 *    tracked but not yet pursued. Injuries and ailments are not simulated yet.
 *  - Memory is per-colonist, line-of-sight knowledge grouped by capability
 *    (Food / Water / Resources / Colonists / Threats); each entry is a last-seen
 *    location that goes stale until the colonist revisits it.
 *  - Tasks are the colony goal registry seen from this colonist. They self-pick
 *    the highest-priority known job every half-second; there are no manual
 *    orders yet (Work Priorities is the planned per-colonist control). */

export type NeedTier = "vital" | "comfort";
export interface ColonistNeed {
	name: string;
	icon: IconName;
	value: number; // 0..1
	seekAt: number; // below this, the colonist seeks to fulfill it
	critAt: number; // below this, it is an emergency
	tier: NeedTier; // vital = AI acts on it today; comfort = tracked only
}
export const DOSSIER_NEEDS: ColonistNeed[] = [
	{ name: "Hunger", icon: "food", value: 0.62, seekAt: 0.5, critAt: 0.1, tier: "vital" },
	{ name: "Thirst", icon: "water", value: 0.8, seekAt: 0.5, critAt: 0.1, tier: "vital" },
	{ name: "Energy", icon: "energy", value: 0.24, seekAt: 0.3, critAt: 0.1, tier: "vital" },
	{ name: "Bladder", icon: "rain", value: 0.55, seekAt: 0.3, critAt: 0.1, tier: "vital" },
	{ name: "Digestion", icon: "leaf", value: 0.71, seekAt: 0.3, critAt: 0.1, tier: "vital" },
	{ name: "Hygiene", icon: "sprout", value: 0.48, seekAt: 0.4, critAt: 0.15, tier: "comfort" },
	{ name: "Recreation", icon: "heart", value: 0.4, seekAt: 0.3, critAt: 0.1, tier: "comfort" },
	{ name: "Temperature", icon: "temp", value: 0.66, seekAt: 0.4, critAt: 0.15, tier: "comfort" },
];

/* Mood is derived from needs in-engine; this is a representative precomputed value.
 * Labels: Happy >=80, Content 60-79, Neutral 40-59, Stressed 20-39, Miserable <20. */
export const DOSSIER_MOOD = 0.72;
export function moodLabel(mood: number): string {
	const m = mood * 100;
	if (m >= 80) return "Happy";
	if (m >= 60) return "Content";
	if (m >= 40) return "Neutral";
	if (m >= 20) return "Stressed";
	return "Miserable";
}
/* a need's standing against its own thresholds -> meter tone */
export function needTone(n: ColonistNeed): "ok" | "warn" | "crit" {
	if (n.value < n.critAt) return "crit";
	if (n.value < n.seekAt) return "warn";
	return "ok";
}

/* ----- Memory ----- */
export interface KnownThing {
	name: string;
	dist: string; // distance from the colonist, e.g. "12m"
	seen: string; // how long since last observed, e.g. "just now", "2m"
	stale?: boolean; // snapshot may be wrong; not revisited recently
}
export interface MemoryCategory {
	name: string;
	icon: IconName;
	tone: "ok" | "data" | "accent" | "warn" | "crit" | "default";
	count: number; // total known in this category
	things: KnownThing[]; // a sample; the real list can run to hundreds
}
export const DOSSIER_SIGHT_M = 30;
export const MEMORY_CATEGORIES: MemoryCategory[] = [
	{
		name: "Food Sources",
		icon: "food",
		tone: "ok",
		count: 9,
		things: [
			{ name: "Berry Bush", dist: "12m", seen: "just now" },
			{ name: "Berry Bush", dist: "18m", seen: "30s" },
			{ name: "Cloudfruit Tree", dist: "44m", seen: "2m", stale: true },
		],
	},
	{
		name: "Water Sources",
		icon: "water",
		tone: "data",
		count: 3,
		things: [
			{ name: "River Shore", dist: "26m", seen: "just now" },
			{ name: "Rain Cistern", dist: "8m", seen: "1m" },
		],
	},
	{
		name: "Resources",
		icon: "box",
		tone: "accent",
		count: 21,
		things: [
			{ name: "Oak (sticks)", dist: "15m", seen: "just now" },
			{ name: "Stone Outcrop", dist: "33m", seen: "4m", stale: true },
			{ name: "Reed Cluster", dist: "52m", seen: "6m", stale: true },
		],
	},
	{
		name: "Colonists",
		icon: "users",
		tone: "default",
		count: 4,
		things: [
			{ name: "Idris Okonkwo", dist: "9m", seen: "just now" },
			{ name: "Rin Calloway", dist: "40m", seen: "1m" },
		],
	},
	{ name: "Threats", icon: "skull", tone: "crit", count: 0, things: [] },
];
export const MEMORY_TOTAL = MEMORY_CATEGORIES.reduce((s, c) => s + c.count, 0);

/* ----- Tasks ----- */
export type TaskRunState = "available" | "active" | "blocked" | "waiting";
export interface KnownTask {
	label: string;
	icon: IconName;
	state: TaskRunState;
	detail: string; // status detail, e.g. "wood ready", "0/2 metal"
	dist: string;
}
export interface CurrentTask {
	type: string; // "Building", "Hauling", ...
	label: string; // target, e.g. "Foundation · Sector 4"
	icon: IconName;
	dist: string;
	nav: string; // "On site", "Going to", "Re-routing", "Can't find a way"
	progress: number; // 0..1
}
export const CURRENT_TASK: CurrentTask = {
	type: "Building",
	label: "Foundation · Sector 4",
	icon: "hammer",
	dist: "6m",
	nav: "On site",
	progress: 0.62,
};
export const KNOWN_TASKS: KnownTask[] = [
	{ label: "Haul Wood ×6 → Stockpile A", icon: "box", state: "available", detail: "wood ready", dist: "8m" },
	{ label: "Harvest Berry Bush", icon: "leaf", state: "available", detail: "ripe", dist: "12m" },
	{ label: "Craft Hatchet", icon: "gear", state: "blocked", detail: "0/2 metal", dist: "15m" },
	{ label: "Cook Meals ×4", icon: "food", state: "blocked", detail: "needs campfire", dist: "22m" },
	{ label: "Haul Stone ×12 → Stockpile A", icon: "box", state: "waiting", detail: "awaiting mining", dist: "33m" },
	{ label: "Mine Stone Outcrop", icon: "mountain", state: "available", detail: "exposed", dist: "33m" },
	{ label: "Deconstruct Pod Wreck", icon: "layers", state: "available", detail: "salvage", dist: "48m" },
];
export function taskStateMeta(s: TaskRunState): { label: string; tone: "ok" | "data" | "warn" | "crit" | "default" } {
	switch (s) {
		case "active":
			return { label: "In Progress", tone: "data" };
		case "available":
			return { label: "Available", tone: "ok" };
		case "blocked":
			return { label: "Blocked", tone: "crit" };
		case "waiting":
			return { label: "Waiting", tone: "warn" };
	}
}
