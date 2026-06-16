/*
 * Flatten the icon set (icons.json) into compiled C++ glyph geometry:
 * libs/ui/theme/IconGlyphs.h. Each glyph becomes one or more subpaths
 * of 2D points in the 24x24 icon space, plus a filled flag. The C++ Icon
 * primitive strokes open/closed subpaths (drawLine + round joins) or fills them
 * (Tessellator) depending on the flag. No runtime SVG parsing.
 *
 * Run from docs/ui-prototype:  node scripts/gen-icon-geometry.mjs
 *
 * The icon path data uses only absolute commands (M L H V C Z) plus <circle>
 * and <rect> elements; beziers are flattened by subdivision.
 */
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const icons = JSON.parse(readFileSync(resolve(here, "../../design/ui/design-system/icons.json"), "utf8"));
const outPath = resolve(here, "../../../libs/ui/theme/IconGlyphs.h");

const CURVE_STEPS = 16;
const CIRCLE_STEPS = 28;

function flattenCubic(pts, x0, y0, x1, y1, x2, y2, x3, y3) {
	for (let k = 1; k <= CURVE_STEPS; k++) {
		const t = k / CURVE_STEPS;
		const u = 1 - t;
		const x = u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x3;
		const y = u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y3;
		pts.push([x, y]);
	}
}

function parsePathData(d) {
	const subpaths = [];
	const tokens = d.match(/[MLHVCZ]|-?\d*\.?\d+/g) || [];
	let i = 0;
	const num = () => parseFloat(tokens[i++]);
	let cmd = null;
	let cur = null;
	let cx = 0;
	let cy = 0;
	let sx = 0;
	let sy = 0;
	while (i < tokens.length) {
		if (/[MLHVCZ]/.test(tokens[i])) cmd = tokens[i++];
		if (cmd === "Z") {
			if (cur) {
				cur.closed = true;
				subpaths.push(cur);
				cur = null;
			}
			cx = sx;
			cy = sy;
			continue;
		}
		if (cmd === "M") {
			const x = num();
			const y = num();
			if (cur) subpaths.push(cur);
			cur = { pts: [[x, y]], closed: false };
			cx = x;
			cy = y;
			sx = x;
			sy = y;
			cmd = "L"; // implicit lineto for subsequent coordinate pairs
			continue;
		}
		if (cmd === "L") {
			const x = num();
			const y = num();
			cur.pts.push([x, y]);
			cx = x;
			cy = y;
			continue;
		}
		if (cmd === "H") {
			const x = num();
			cur.pts.push([x, cy]);
			cx = x;
			continue;
		}
		if (cmd === "V") {
			const y = num();
			cur.pts.push([cx, y]);
			cy = y;
			continue;
		}
		if (cmd === "C") {
			const x1 = num();
			const y1 = num();
			const x2 = num();
			const y2 = num();
			const x = num();
			const y = num();
			flattenCubic(cur.pts, cx, cy, x1, y1, x2, y2, x, y);
			cx = x;
			cy = y;
			continue;
		}
		i++; // safety: skip anything unexpected
	}
	if (cur) subpaths.push(cur);
	return subpaths;
}

function circleSubpath(cx, cy, r) {
	const pts = [];
	for (let k = 0; k < CIRCLE_STEPS; k++) {
		const a = (k / CIRCLE_STEPS) * Math.PI * 2;
		pts.push([cx + Math.cos(a) * r, cy + Math.sin(a) * r]);
	}
	return { pts, closed: true };
}

function rectSubpath(x, y, w, h) {
	return { pts: [[x, y], [x + w, y], [x + w, y + h], [x, y + h]], closed: true };
}

function attr(s, name) {
	const m = s.match(new RegExp(`${name}="([^"]+)"`));
	return m ? parseFloat(m[1]) : 0;
}

function glyphSubpaths(svg) {
	const subpaths = [];
	for (const m of svg.matchAll(/<path d="([^"]+)"/g)) subpaths.push(...parsePathData(m[1]));
	for (const m of svg.matchAll(/<circle ([^>]*?)\/?>/g)) subpaths.push(circleSubpath(attr(m[1], "cx"), attr(m[1], "cy"), attr(m[1], "r")));
	for (const m of svg.matchAll(/<rect ([^>]*?)\/?>/g)) subpaths.push(rectSubpath(attr(m[1], "x"), attr(m[1], "y"), attr(m[1], "width"), attr(m[1], "height")));
	return subpaths;
}

const r3 = (n) => {
	const v = Math.round(n * 1000) / 1000;
	return Number.isInteger(v) ? `${v}.0F` : `${v}F`;
};

const names = Object.keys(icons.glyphs);
const out = [];
out.push(`#pragma once

// GENERATED from docs/ui-prototype/src/design-system/primitives/Icon/Icon.tsx
// (via icons.json) by docs/ui-prototype/scripts/gen-icon-geometry.mjs.
// Do not edit by hand.
//
// Flattened glyph geometry in the 24x24 icon space. Stroked glyphs are drawn as
// line segments with round joins; filled glyphs are tessellated. See Icon.cpp.

#include "math/Types.h"

#include <string_view>

namespace UI::Icons {

	struct SubPath {
		const Foundation::Vec2* pts;
		int						count;
		bool					closed;
	};

	struct GlyphDef {
		const SubPath* subs;
		int			   subCount;
		bool		   filled;
	};
`);

let totalPts = 0;
for (const name of names) {
	const g = icons.glyphs[name];
	const subs = glyphSubpaths(g.svg);
	const subNames = [];
	subs.forEach((sp, idx) => {
		totalPts += sp.pts.length;
		const arr = sp.pts.map(([x, y]) => `{${r3(x)}, ${r3(y)}}`).join(", ");
		out.push(`\tinline const Foundation::Vec2 ${name}_p${idx}[] = {${arr}};`);
		subNames.push({ var: `${name}_p${idx}`, count: sp.pts.length, closed: sp.closed });
	});
	const subList = subNames.map((s) => `{${s.var}, ${s.count}, ${s.closed}}`).join(", ");
	out.push(`\tinline const SubPath ${name}_subs[] = {${subList}};`);
	out.push(`\tinline const GlyphDef ${name}_def{${name}_subs, ${subNames.length}, ${g.filled}};`);
	out.push("");
}

out.push(`\tstruct Entry {
		std::string_view name;
		const GlyphDef*	 def;
	};

	inline const Entry registry[] = {`);
out.push(names.map((n) => `\t\t{"${n}", &${n}_def}`).join(",\n") + "};");
out.push(`
	inline const GlyphDef* find(std::string_view name) {
		for (const Entry& e : registry) {
			if (e.name == name) return e.def;
		}
		return nullptr;
	}

	inline constexpr int count = ${names.length};

} // namespace UI::Icons
`);

mkdirSync(dirname(outPath), { recursive: true });
writeFileSync(outPath, out.join("\n") + "\n");
console.log(`Wrote ${names.length} glyphs (${totalPts} points) to ${outPath}`);
