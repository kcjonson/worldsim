import { useId } from "react";
import styles from "./Slider.module.css";

export interface SliderProps {
	label: string;
	value: number;
	min: number;
	max: number;
	step?: number;
	unit?: string;
	/* normalized 0..1 position of an "ideal / earth-like" detent marker */
	detent?: number;
	onChange?: (value: number) => void;
	format?: (value: number) => string;
}

export function Slider({ label, value, min, max, step = 1, unit, detent, onChange, format }: SliderProps) {
	const id = useId();
	const pct = ((value - min) / (max - min)) * 100;
	const display = format ? format(value) : `${value}${unit ? ` ${unit}` : ""}`;

	return (
		<div className={styles.wrap}>
			<div className={styles.head}>
				<label htmlFor={id} className={styles.label}>
					{label}
				</label>
				<output className={styles.value}>{display}</output>
			</div>
			<div className={styles.trackWrap}>
				<div className={styles.track}>
					<div className={styles.fill} style={{ width: `${pct}%` }} />
				</div>
				{detent !== undefined && <span className={styles.detent} style={{ left: `${detent * 100}%` }} />}
				<input
					id={id}
					className={styles.input}
					type="range"
					min={min}
					max={max}
					step={step}
					value={value}
					onChange={(e) => onChange?.(Number(e.target.value))}
				/>
			</div>
		</div>
	);
}
