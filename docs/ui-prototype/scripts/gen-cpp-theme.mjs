/*
 * Generate worldsim's theme from the canonical tokens.json: the same tokens as a
 * Design Tokens file (Format Module 2025.10) for salal's salal_tokens(),
 * libs/theme/tokens/theme.tokens.json, worldsim's own names under `theme`,
 * salal ui's semantic tokens aliased to them, and ui's component styles set to
 * the looks worldsim's own components had before it moved onto salal's ui.
 *
 * Run from docs/ui-prototype:  node scripts/gen-cpp-theme.mjs
 */
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const tokens = JSON.parse(readFileSync(resolve(here, "../../design/ui/design-system/tokens.json"), "utf8"));
const dtcgPath = resolve(here, "../../../libs/theme/tokens/theme.tokens.json");

// Design Tokens. Z layers have no place in salal's ordering, and font stacks, transforms and
// layered or inset shadows have no token type salal reads, so they stay CSS-only.
const px = (value) => ({ value, unit: "px" });
const hex2 = (c) => Math.round(c * 255).toString(16).padStart(2, "0");
const dtcgColor = ([r, g, b, a]) => ({
	colorSpace: "srgb",
	components: [r, g, b],
	...(a < 1 ? { alpha: a } : {}),
	hex: `#${hex2(r)}${hex2(g)}${hex2(b)}`,
});
const bezier = (css) => css.match(/^cubic-bezier\(([^)]+)\)$/)?.[1].split(",").map(Number);
const shadow = (css) => {
	const m = css.match(/^(-?[\d.]+)(?:px)? (-?[\d.]+)(?:px)? ([\d.]+)(?:px)?(?: ([\d.]+)(?:px)?)? rgba\(([^)]+)\)$/);
	if (!m) return undefined;
	const [r, g, b, a] = m[5].split(",").map(Number);
	return {
		color: dtcgColor([r / 255, g / 255, b / 255, a].map((c) => Math.round(c * 10000) / 10000)),
		offsetX: px(Number(m[1])),
		offsetY: px(Number(m[2])),
		blur: px(Number(m[3])),
		spread: px(Number(m[4] ?? 0)),
	};
};

// [type, value] for an entry with a value of its own, or undefined where it has none salal reads.
function dtcgToken(cat, e) {
	switch (cat) {
		case "color":
			return ["color", dtcgColor(e.rgba)];
		case "spacing":
		case "radius":
		case "border":
		case "fontSize":
			return ["dimension", px(e.value)];
		case "lineHeight":
		case "letterSpacing":
		case "texture":
			return ["number", e.value];
		case "motion":
			return e.cssOnly ? (bezier(e.css) ? ["cubicBezier", bezier(e.css)] : undefined) : ["duration", { value: e.value, unit: "ms" }];
		case "typography":
			return e.cssOnly ? undefined : ["fontWeight", e.value];
		case "shadow":
			return shadow(e.css) ? ["shadow", shadow(e.css)] : undefined;
		default:
			return undefined;
	}
}

const own = {};
const paths = new Map(); // CSS name -> [token path, type]
const values = new Map(); // CSS name -> its number, an alias's resolved
const aliases = [];
for (const [cat, group] of Object.entries(tokens)) {
	if (typeof group !== "object") continue;
	for (const [name, e] of Object.entries(group)) {
		if (typeof e.value === "number") values.set(name, e.value);
		if (e.alias) {
			aliases.push([cat, name, e.alias.match(/^var\(--([\w-]+)\)$/)?.[1]]);
			continue;
		}
		const token = dtcgToken(cat, e);
		if (!token) continue;
		(own[cat] ??= {})[name] = { $type: token[0], $value: token[1] };
		paths.set(name, [`theme.${cat}.${name}`, token[0]]);
	}
}
for (const [cat, name, target] of aliases) {
	const resolved = paths.get(target);
	if (!resolved) continue; // an alias of a CSS-only token
	(own[cat] ??= {})[name] = { $type: resolved[1], $value: `{${resolved[0]}}` };
	paths.set(name, [`theme.${cat}.${name}`, resolved[1]]);
}

// ui's tokens as worldsim's: [ui path, CSS name]. Style fields take no $type.
const mapping = [
	["color.surface.base", "bg-base"],
	["color.surface.raised", "bg-panel"],
	["color.surface.sunken", "bg-inset"],
	["color.surface.overlay", "bg-panel-raised"],
	["color.surface.control", "bg-panel-raised"],
	["color.text.primary", "text"],
	["color.text.secondary", "text-dim"],
	["color.text.disabled", "text-disabled"],
	["color.text.inverse", "accent-contrast"],
	["color.line.subtle", "line-hairline"],
	["color.line.edge", "line-edge"],
	["color.line.strong", "line-strong"],
	["color.scrim", "scrim"],
	["color.focus", "accent"],
	["space.xxs", "space-0-5"],
	["space.xs", "space-1"],
	["space.sm", "space-2"],
	["space.md", "space-3"],
	["space.lg", "space-4"],
	["space.xl", "space-6"],
	["space.xxl", "space-8"],
	["radius.none", "r-0"],
	["radius.sm", "r-sm"],
	["radius.md", "r-md"],
	["radius.lg", "r-lg"],
	["radius.pill", "r-pill"],
	["lineWidth.thin", "bw"],
	["lineWidth.thick", "bw-thick"],
	["motion.duration.fast", "dur-fast"],
	["motion.duration.normal", "dur"],
	["motion.duration.slow", "dur-slow"],
	["motion.easing.standard", "ease"],
	["motion.easing.enter", "ease-out"],
	["size.sm.fontSize", "fs-sm"],
	["size.md.fontSize", "fs-base"],
	["size.lg.fontSize", "fs-md"],
	["tone.accent.color", "accent"],
	["tone.accent.subtle", "bg-active"],
	["tone.accent.onColor", "accent-contrast"],
	["tone.info.color", "status-info"],
	["tone.positive.color", "status-ok"],
	["tone.warning.color", "status-warn"],
	["tone.danger.color", "status-crit"],
	["state.hover.wash", "bg-hover"],
	["state.active.wash", "bg-active"],
	["focusRing.width", "bw"],
	["shadow.overlay", "shadow-pop"],
];

// ui's component styles in the looks worldsim's own components had: [style field, value]. A string
// is a CSS name and {ui} one of ui's tokens, both aliased; {em: [spacing, size]} is an em letter
// spacing at a font size, which a style takes in px; {shadow: "none"} is a clear shadow, so the
// style casts none.
const styleFields = [
	["pane.borderRadius", "radius-panel"],
	["button.backgroundColor", { ui: "base.color.clear" }],
	["button.borderRadius", "radius-ui"],
	["button.fontFamily", { ui: "font.family.display" }],
	["button.letterSpacing", { em: ["ls-wide", "fs-sm"] }],
	["label.fontSize", "fs-2xs"],
	["label.letterSpacing", { em: ["label-spacing", "fs-2xs"] }],
	["menuItem.borderRadius", "r-0"],
	["overlay.borderColor", "line-edge"],
	["overlay.borderRadius", "radius-panel"],
	["segment.fontFamily", { ui: "font.family.display" }],
	["segment.letterSpacing", { em: ["ls-wide", "fs-sm"] }],
	["select.backgroundColor", "bg-inset"],
	["select.borderColor", "line-hairline"],
	["select.borderRadius", "radius-ui"],
	["select.color", "text-bright"],
	["statValue.color", "text-bright"],
	["tab.fontFamily", { ui: "font.family.display" }],
	["tab.fontSize", "fs-sm"],
	["tab.letterSpacing", { em: ["ls-wide", "fs-sm"] }],
	["textInput.borderColor", "line-hairline"],
	["textInput.color", "text-bright"],
	["textInput.fontSize", "fs-base"],
	["toast.borderColor", "line-edge"],
	["toast.borderRadius", "radius-panel"],
	["tooltip.borderColor", "line-edge"],
	["tooltip.boxShadow", { shadow: "none" }],
];

// Text transforms, which salal reads from its extension on a style's group.
const styleTransforms = [
	["button", "uppercase"],
	["segment", "uppercase"],
	["tab", "uppercase"],
];

function styleField(field, value) {
	if (typeof value === "string") {
		const resolved = paths.get(value);
		if (!resolved) throw new Error(`styles.${field}: no token ${value}`);
		return { $value: `{${resolved[0]}}` };
	}
	if (value.ui) return { $value: `{${value.ui}}` };
	if (value.em) {
		const [spacing, size] = value.em.map((name) => values.get(name));
		if (spacing === undefined || size === undefined) throw new Error(`styles.${field}: no token in ${value.em}`);
		return { $type: "dimension", $value: px(Math.round(spacing * size * 10000) / 10000) };
	}
	if (value.shadow === "none") {
		return { $type: "shadow", $value: { color: dtcgColor([0, 0, 0, 0]), offsetX: px(0), offsetY: px(0), blur: px(0), spread: px(0) } };
	}
	throw new Error(`styles.${field}: unknown value ${JSON.stringify(value)}`);
}

const dtcg = {
	$description:
		"GENERATED from docs/ui-prototype/src/design-system/tokens.css via tokens.json by docs/ui-prototype/scripts/gen-cpp-theme.mjs. Do not edit by hand. Worldsim's theme over salal ui's default tokens: its own names under theme, ui's semantic tokens aliased to them, and ui's component styles in worldsim's looks.",
	theme: own,
};
const place = (path, token) => {
	const segments = path.split(".");
	let group = dtcg;
	for (const segment of segments.slice(0, -1)) group = group[segment] ??= {};
	group[segments.at(-1)] = token;
};
for (const [uiPath, name] of mapping) {
	const resolved = paths.get(name);
	if (!resolved) throw new Error(`${uiPath}: no token ${name}`);
	place(uiPath, { $type: resolved[1], $value: `{${resolved[0]}}` });
}
for (const [field, value] of styleFields) place(`styles.${field}`, styleField(field, value));
for (const [style, transform] of styleTransforms) place(`styles.${style}.$extensions`, { salal: { textTransform: transform } });

mkdirSync(dirname(dtcgPath), { recursive: true });
writeFileSync(dtcgPath, JSON.stringify(dtcg, null, 2) + "\n");
console.log(
	`Wrote ${paths.size} tokens, ${mapping.length} of ui's, ${styleFields.length} style fields and ${styleTransforms.length} transforms to ${dtcgPath}`,
);
