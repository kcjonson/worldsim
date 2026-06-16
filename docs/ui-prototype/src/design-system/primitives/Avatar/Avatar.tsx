import styles from "./Avatar.module.css";

export interface AvatarProps {
	seed: string;
	size?: number;
	/* 0..1 mood, tints the frame ring */
	mood?: number;
	selected?: boolean;
	className?: string;
}

function hash(str: string): number {
	let h = 2166136261;
	for (let i = 0; i < str.length; i++) {
		h ^= str.charCodeAt(i);
		h = Math.imul(h, 16777619);
	}
	return h >>> 0;
}

function moodColor(mood?: number): string {
	if (mood === undefined) return "var(--line-edge)";
	if (mood < 0.3) return "var(--status-crit)";
	if (mood < 0.55) return "var(--status-warn)";
	return "var(--status-ok)";
}

export function Avatar({ seed, size = 44, mood, selected, className }: AvatarProps) {
	const h = hash(seed);
	const hue = h % 360;
	const hue2 = (hue + 40) % 360;
	const ring = moodColor(mood);
	const initials = seed
		.split(/\s+/)
		.map((w) => w[0])
		.slice(0, 2)
		.join("")
		.toUpperCase();

	const gradId = `g${h.toString(36)}`;

	return (
		<div
			className={[styles.frame, selected ? styles.selected : "", className ?? ""].filter(Boolean).join(" ")}
			style={{ width: size, height: size, borderColor: ring, boxShadow: mood !== undefined ? `0 0 8px ${ring}55` : undefined }}
		>
			<svg viewBox="0 0 44 44" width="100%" height="100%" aria-label={seed}>
				<defs>
					<linearGradient id={gradId} x1="0" y1="0" x2="1" y2="1">
						<stop offset="0" stopColor={`hsl(${hue} 45% 24%)`} />
						<stop offset="1" stopColor={`hsl(${hue2} 50% 14%)`} />
					</linearGradient>
				</defs>
				<rect width="44" height="44" fill={`url(#${gradId})`} />
				{/* head + shoulders silhouette */}
				<circle cx="22" cy="17" r="7.5" fill={`hsl(${hue} 30% 60% / 0.55)`} />
				<path d="M8 44C8 33 14 28 22 28C30 28 36 33 36 44Z" fill={`hsl(${hue} 30% 60% / 0.55)`} />
			</svg>
			<span className={styles.initials} style={{ color: `hsl(${hue} 55% 82%)` }}>
				{initials}
			</span>
			<span className={styles.tick} />
		</div>
	);
}
