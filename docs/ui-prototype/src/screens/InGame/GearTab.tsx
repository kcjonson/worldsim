import { Icon, Meter } from "../../design-system";
import {
	WORN_SLOTS,
	CARRIED,
	BELT_SLOTS,
	BELT_SLOT_COUNT,
	HELD,
	PACK_CAP_KG,
	CARRY_CAP_KG,
	stackKg,
	packKg,
	beltKg,
	totalCarryKg,
	fitTag,
	fitLabel,
	type WornSlot,
	type ItemStack,
	type GearItem,
	type HeldGear,
} from "../../data/mock";
import styles from "./GearTab.module.css";

function SlotBox({ slot }: { slot: WornSlot }) {
	return (
		<div className={[styles.slot, slot.item ? styles.filled : styles.empty].join(" ")}>
			<span className={styles.slotLabel}>{slot.label}</span>
			{slot.item ? (
				<div className={styles.slotItem}>
					<Icon name={slot.item.icon} size={16} className={styles.slotIcon} />
					<span className={styles.slotName}>{slot.item.name}</span>
				</div>
			) : (
				<div className={styles.slotEmptyRow}>
					<Icon name="plus" size={12} />
					<span>Empty</span>
				</div>
			)}
		</div>
	);
}

function HeldSlot({ label, item, span }: { label: string; item: HeldGear | null; span?: boolean }) {
	return (
		<div className={[styles.slot, item ? styles.filled : styles.empty, span ? styles.span : ""].filter(Boolean).join(" ")}>
			<span className={styles.slotLabel}>{label}</span>
			{item ? (
				<div className={styles.slotItem}>
					<Icon name={item.icon} size={16} className={styles.slotIcon} />
					<span className={styles.slotName}>{item.name}</span>
					<span className={styles.fit}>{fitTag(item.hands)}</span>
				</div>
			) : (
				<div className={styles.slotEmptyRow}>
					<Icon name="plus" size={12} />
					<span>Empty</span>
				</div>
			)}
		</div>
	);
}

function StackRow({ stack }: { stack: ItemStack }) {
	return (
		<div
			className={styles.stackRow}
			title={`${stack.name} ×${stack.qty} · ${fitLabel(stack.hands)} · ${stackKg(stack).toFixed(1)} kg`}
		>
			<Icon name={stack.icon} size={14} className={styles.stackIcon} />
			<span className={styles.stackName}>{stack.name}</span>
			{stack.qty > 1 && <span className={styles.stackQty}>×{stack.qty}</span>}
			<span className={styles.stackFit}>{fitTag(stack.hands)}</span>
			<span className={styles.stackKg}>{stackKg(stack).toFixed(1)} kg</span>
		</div>
	);
}

function BeltCell({ item }: { item: GearItem | null }) {
	if (!item) {
		return (
			<div className={[styles.beltCell, styles.beltEmpty].join(" ")} aria-label="Empty belt slot">
				<Icon name="plus" size={12} />
			</div>
		);
	}
	return (
		<div className={styles.beltCell} title={`${item.name} · one-hand${item.kg ? ` · ${item.kg.toFixed(1)} kg` : ""}`}>
			<Icon name={item.icon} size={18} className={styles.beltIcon} />
		</div>
	);
}

export function GearTab() {
	const left = WORN_SLOTS.filter((s) => s.col === "left");
	const right = WORN_SLOTS.filter((s) => s.col === "right");
	const packName = WORN_SLOTS.find((s) => s.key === "back")?.item?.name ?? "Back";
	const beltName = WORN_SLOTS.find((s) => s.key === "belt")?.item?.name ?? "Belt";
	const packUsed = packKg();
	const beltFilled = BELT_SLOTS.filter(Boolean).length;
	const total = totalCarryKg();

	return (
		<div className={styles.gear}>
			<div className={styles.paperdoll}>
				<div className={styles.col}>
					{left.map((s) => (
						<SlotBox key={s.key} slot={s} />
					))}
				</div>

				<div className={styles.figureWrap}>
					<svg className={styles.figure} viewBox="0 0 64 150" aria-hidden="true">
						<circle cx="32" cy="18" r="11" />
						<path d="M21 30 H43 L47 44 L42 47 L42 78 H22 V47 L17 44 Z" />
						<path d="M19 31 L9 64 L14 67 L23 42 Z" />
						<path d="M45 31 L55 64 L50 67 L41 42 Z" />
						<path d="M24 78 H31 V120 L29 146 H23 L25 118 Z" />
						<path d="M40 78 H33 V120 L35 146 H41 L39 118 Z" />
					</svg>
				</div>

				<div className={styles.col}>
					{right.map((s) => (
						<SlotBox key={s.key} slot={s} />
					))}
					{HELD.twoHanded ? (
						<HeldSlot label="Both Hands" item={HELD.twoHanded} span />
					) : (
						<>
							<HeldSlot label="Left Hand" item={HELD.left} />
							<HeldSlot label="Right Hand" item={HELD.right} />
						</>
					)}
				</div>
			</div>

			{/* Pack — weight-based */}
			<div className={styles.container}>
				<div className={styles.cHead}>
					<span className={styles.cName}>{packName}</span>
					<span className={styles.cWeight}>
						{packUsed.toFixed(1)} / {PACK_CAP_KG} kg
					</span>
				</div>
				<Meter value={packUsed / PACK_CAP_KG} tone={packUsed / PACK_CAP_KG > 0.85 ? "warn" : "data"} size="sm" />
				<div className={styles.cAccepts}>Weight-based · pocket &amp; one-hand items</div>
				<div className={styles.stacks}>
					{CARRIED.map((s) => (
						<StackRow key={s.name} stack={s} />
					))}
				</div>
			</div>

			{/* Belt — discrete one-hand slots */}
			<div className={styles.container}>
				<div className={styles.cHead}>
					<span className={styles.cName}>{beltName}</span>
					<span className={styles.cWeight}>
						{beltFilled} / {BELT_SLOT_COUNT} slots · {beltKg().toFixed(1)} kg
					</span>
				</div>
				<div className={styles.cAccepts}>Quick-draw slots · one-hand tools only</div>
				<div className={styles.beltSlots}>
					{BELT_SLOTS.map((it, i) => (
						<BeltCell key={i} item={it} />
					))}
				</div>
			</div>

			<div className={styles.total}>
				<Meter
					label="Carry load"
					value={total / CARRY_CAP_KG}
					valueText={`${total.toFixed(1)} / ${CARRY_CAP_KG} kg`}
					tone={total / CARRY_CAP_KG > 0.85 ? "warn" : "data"}
				/>
			</div>
		</div>
	);
}
