/*
 * Generate the C++ design-system token header from the canonical tokens.json.
 * Compile-time constexpr, consumed by the Salvage primitive library.
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
