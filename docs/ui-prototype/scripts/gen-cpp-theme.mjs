/*
 * Generate the C++ design-system token header from the canonical tokens.json.
 * Compile-time constexpr, consumed by the Salvage primitive library. Also writes
 * the same tokens as a Design Tokens file (Format Module 2025.10) for salal's
 * salal_tokens(), libs/salvage/tokens/salvage.tokens.json: worldsim's own names
 * under `salvage`, and salal ui's semantic tokens aliased to them.
 *
 * Run from docs/ui-prototype:  node scripts/gen-cpp-theme.mjs
 *
 * Flat UI namespace, snake_case names mirroring the CSS custom properties
 * (--space-3 -> UI::space_3, --accent -> UI::accent) so the mapping is
 * obvious. Colors become Foundation::Color (rgba floats from tokens.json),
 * z-index becomes int, everything else float. cssOnly tokens (gradients, eases,
 * font stacks) and pure aliases are skipped.
 */
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const tokens = JSON.parse(readFileSync(resolve(here, "../../design/ui/design-system/tokens.json"), "utf8"));
const outPath = resolve(here, "../../../libs/ui/theme/Tokens.h");
const dtcgPath = resolve(here, "../../../libs/salvage/tokens/salvage.tokens.json");

const id = (name) => name.replace(/-/g, "_");
const f = (n) => (Number.isInteger(n) ? `${n}.0F` : `${n}F`);

// Category -> { ns comment, emit fn }. Order is the emission order.
const order = ["color", "spacing", "radius", "border", "fontSize", "lineHeight", "letterSpacing", "motion", "zIndex", "texture", "typography"];
const headings = {
	color: "Colors",
	spacing: "Spacing (px)",
	radius: "Radius (px)",
	border: "Border widths (px)",
	fontSize: "Font sizes (px)",
	lineHeight: "Line heights",
	letterSpacing: "Letter spacing (em)",
	motion: "Motion (ms)",
	zIndex: "Z layers",
	texture: "Density & texture",
	typography: "Typography (numeric)",
};

const lines = [];
let count = 0;
for (const cat of order) {
	const group = tokens[cat];
	if (!group) continue;
	const entries = [];
	for (const [name, e] of Object.entries(group)) {
		if (e.cssOnly || e.alias) continue; // skip effects and pure aliases
		if (cat === "color") {
			const [r, g, b, a] = e.rgba;
			entries.push(`\tinline constexpr Foundation::Color ${id(name)}{${f(r)}, ${f(g)}, ${f(b)}, ${f(a)}};`);
		} else if (cat === "zIndex") {
			entries.push(`\tinline constexpr int ${id(name)} = ${e.value};`);
		} else if (typeof e.value === "number") {
			entries.push(`\tinline constexpr float ${id(name)} = ${f(e.value)};`);
		}
	}
	if (!entries.length) continue;
	lines.push(`\t// ${headings[cat]}`);
	lines.push(...entries);
	lines.push("");
	count += entries.length;
}

const header = `#pragma once

// GENERATED from docs/ui-prototype/src/design-system/tokens.css via tokens.json
// by docs/ui-prototype/scripts/gen-cpp-theme.mjs. Do not edit by hand.
//
// Salvage design tokens for the C++ UI. Compile-time constexpr. Names mirror
// the CSS custom properties: --space-3 -> UI::space_3, --accent -> UI::accent.

#include "graphics/Color.h"

namespace UI {

${lines.join("\n")}} // namespace UI
`;

mkdirSync(dirname(outPath), { recursive: true });
writeFileSync(outPath, header);
console.log(`Wrote ${count} tokens to ${outPath}`);

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

const salvage = {};
const paths = new Map(); // CSS name -> [token path, type]
const aliases = [];
for (const [cat, group] of Object.entries(tokens)) {
	if (typeof group !== "object") continue;
	for (const [name, e] of Object.entries(group)) {
		if (e.alias) {
			aliases.push([cat, name, e.alias.match(/^var\(--([\w-]+)\)$/)?.[1]]);
			continue;
		}
		const token = dtcgToken(cat, e);
		if (!token) continue;
		(salvage[cat] ??= {})[name] = { $type: token[0], $value: token[1] };
		paths.set(name, [`salvage.${cat}.${name}`, token[0]]);
	}
}
for (const [cat, name, target] of aliases) {
	const resolved = paths.get(target);
	if (!resolved) continue; // an alias of a CSS-only token
	(salvage[cat] ??= {})[name] = { $type: resolved[1], $value: `{${resolved[0]}}` };
	paths.set(name, [`salvage.${cat}.${name}`, resolved[1]]);
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
	["styles.pane.borderRadius", "radius-panel"],
	["styles.button.borderRadius", "radius-ui"],
];

const dtcg = {
	$description:
		"GENERATED from docs/ui-prototype/src/design-system/tokens.css via tokens.json by docs/ui-prototype/scripts/gen-cpp-theme.mjs. Do not edit by hand. Worldsim's Salvage theme over salal ui's default tokens: its own names under salvage, and ui's semantic tokens aliased to them.",
	salvage,
};
for (const [uiPath, name] of mapping) {
	const resolved = paths.get(name);
	if (!resolved) throw new Error(`${uiPath}: no token ${name}`);
	const segments = uiPath.split(".");
	let group = dtcg;
	for (const segment of segments.slice(0, -1)) group = group[segment] ??= {};
	group[segments.at(-1)] = uiPath.startsWith("styles.") ? { $value: `{${resolved[0]}}` } : { $type: resolved[1], $value: `{${resolved[0]}}` };
}

mkdirSync(dirname(dtcgPath), { recursive: true });
writeFileSync(dtcgPath, JSON.stringify(dtcg, null, 2) + "\n");
console.log(`Wrote ${paths.size} tokens and ${mapping.length} of ui's to ${dtcgPath}`);
