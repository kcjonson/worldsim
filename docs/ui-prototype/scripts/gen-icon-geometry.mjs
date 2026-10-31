/*
 * Flatten the icon set (icons.json) into compiled C++ glyph geometry:
 * libs/ui/theme/IconGlyphs.h. Each glyph becomes one or more subpaths
 * of 2D points in the 24x24 icon space, plus a filled flag. The C++ Icon
 * primitive strokes open/closed subpaths (drawLine + round joins) or fills them
 * (Tessellator) depending on the flag. No runtime SVG parsing.
 *
 * Also writes each glyph as a filled SVG for salal's salal_icons(), into
 * libs/salvage/icons/: filled glyphs as they are, stroked ones outlined at the
 * set's stroke width with round caps and joins, each icon one traced shape.
 * plusBold is plus at stroke width 3. assets/ui/icons' ten go there too.
 *
 * Run from docs/ui-prototype:  node scripts/gen-icon-geometry.mjs
 *
 * The icon path data uses only absolute commands (M L H V C Z) plus <circle>
 * and <rect> elements; beziers are flattened by subdivision.
 */
import { readFileSync, readdirSync, writeFileSync, mkdirSync } from "node:fs";
import { basename, dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const icons = JSON.parse(readFileSync(resolve(here, "../../design/ui/design-system/icons.json"), "utf8"));
const outPath = resolve(here, "../../../libs/ui/theme/IconGlyphs.h");
const svgDir = resolve(here, "../../../libs/salvage/icons");
const assetIcons = resolve(here, "../../../assets/ui/icons");

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

// Outlines, traced as one shape so no two contours overlap: msdfgen's field goes wrong where they
// do. A shape is a signed distance, negative inside, and a union of parts is their minimum;
// marching squares follows its zero line on a fine grid, and the loops are thinned to within
// TOLERANCE of it. Loops that add run clockwise on screen, as nanosvg's circles do, holes the
// other way.
const GRID = 0.04;
const TOLERANCE = 0.002;
// Off the grid, so no edge at a round coordinate or a 45 degree slope lands on samples.
const ORIGIN_X = -0.5137;
const ORIGIN_Y = -0.5291;
const SAMPLES = Math.ceil((25 - ORIGIN_X) / GRID) + 1;
const n3 = (v) => String(Math.round(v * 1000) / 1000);
const pt = (x, y) => `${n3(x)} ${n3(y)}`;

function segmentDistance(x, y, [ax, ay], [bx, by]) {
	const dx = bx - ax;
	const dy = by - ay;
	const len2 = dx * dx + dy * dy;
	const t = len2 === 0 ? 0 : Math.max(0, Math.min(1, ((x - ax) * dx + (y - ay) * dy) / len2));
	return Math.hypot(x - ax - t * dx, y - ay - t * dy);
}

function lineDistance(pts, closed) {
	const count = closed ? pts.length : pts.length - 1;
	return (x, y) => {
		let d = Math.hypot(x - pts[0][0], y - pts[0][1]);
		for (let i = 0; i < count; i++) d = Math.min(d, segmentDistance(x, y, pts[i], pts[(i + 1) % pts.length]));
		return d;
	};
}

// A filled polygon, nonzero.
function polygonDistance(pts) {
	const edge = lineDistance(pts, true);
	return (x, y) => {
		let winding = 0;
		for (let i = 0; i < pts.length; i++) {
			const [ax, ay] = pts[i];
			const [bx, by] = pts[(i + 1) % pts.length];
			const side = (bx - ax) * (y - ay) - (x - ax) * (by - ay);
			if (ay <= y && by > y && side > 0) winding++;
			else if (ay > y && by <= y && side < 0) winding--;
		}
		return winding !== 0 ? -edge(x, y) : edge(x, y);
	};
}

// A stroked glyph's parts: each the distance from its stroke's middle, less half the width.
function strokeParts(svg, half) {
	const parts = [];
	for (const [, tag, attrs] of svg.matchAll(/<(path|circle|rect) ([^>]*?)\/?>/g)) {
		if (tag === "path") {
			for (const sp of parsePathData(attrs.match(/d="([^"]+)"/)[1])) {
				const d = lineDistance(sp.pts, sp.closed);
				parts.push((x, y) => d(x, y) - half);
			}
		} else if (tag === "circle") {
			const [cx, cy, r] = ["cx", "cy", "r"].map((a) => attr(attrs, a));
			parts.push((x, y) => Math.abs(Math.hypot(x - cx, y - cy) - r) - half);
		} else {
			const [left, top, w, h, rx] = ["x", "y", "width", "height", "rx"].map((a) => attr(attrs, a));
			parts.push((x, y) => {
				const qx = Math.abs(x - left - w / 2) - w / 2 + rx;
				const qy = Math.abs(y - top - h / 2) - h / 2 + rx;
				return Math.abs(Math.hypot(Math.max(qx, 0), Math.max(qy, 0)) + Math.min(Math.max(qx, qy), 0) - rx) - half;
			});
		}
	}
	return parts;
}

// Douglas-Peucker over a closed loop, split at the point farthest from its first.
function thin(loop) {
	const keep = new Uint8Array(loop.length);
	const span = (a, b) => {
		let worst = -1;
		let far = 0;
		for (let i = a + 1; i < b; i++) {
			const d = segmentDistance(loop[i][0], loop[i][1], loop[a], loop[b % loop.length]);
			if (d > far) [far, worst] = [d, i];
		}
		if (far > TOLERANCE) {
			keep[worst] = 1;
			span(a, worst);
			span(worst, b);
		}
	};
	let split = 0;
	for (let i = 1, far = 0; i < loop.length; i++) {
		const d = Math.hypot(loop[i][0] - loop[0][0], loop[i][1] - loop[0][1]);
		if (d > far) [far, split] = [d, i];
	}
	keep[0] = keep[split] = 1;
	span(0, split);
	span(split, loop.length);
	return loop.filter((_, i) => keep[i]);
}

function trace(parts) {
	const n = SAMPLES;
	const atX = (i) => ORIGIN_X + i * GRID;
	const atY = (j) => ORIGIN_Y + j * GRID;
	const v = new Float64Array(n * n);
	for (let j = 0; j < n; j++) {
		for (let i = 0; i < n; i++) {
			let d = Infinity;
			for (const part of parts) d = Math.min(d, part(atX(i), atY(j)));
			v[j * n + i] = d;
			if (d < 0 && (i === 0 || j === 0 || i === n - 1 || j === n - 1)) throw new Error("an icon runs off the traced grid");
		}
	}
	const inside = (i, j) => v[j * n + i] < 0;
	// A crossing on the edge from (i, j) right (horizontal) or down; its id is unique to the edge.
	const crossing = (i, j, down) => {
		const [i1, j1] = down ? [i, j + 1] : [i + 1, j];
		const a = v[j * n + i];
		const t = a / (a - v[j1 * n + i1]);
		return { id: (j * n + i) * 2 + (down ? 1 : 0), p: down ? [atX(i), atY(j) + t * GRID] : [atX(i) + t * GRID, atY(j)] };
	};
	const next = new Map();
	for (let j = 0; j < n - 1; j++) {
		for (let i = 0; i < n - 1; i++) {
			const c = [inside(i, j), inside(i + 1, j), inside(i + 1, j + 1), inside(i, j + 1)];
			const corners = [[atX(i), atY(j)], [atX(i + 1), atY(j)], [atX(i + 1), atY(j + 1)], [atX(i), atY(j + 1)]];
			// Edges by the corners they join: top 0-1, right 1-2, bottom 3-2, left 0-3.
			const edges = [
				c[0] !== c[1] && crossing(i, j, false),
				c[1] !== c[2] && crossing(i + 1, j, true),
				c[3] !== c[2] && crossing(i, j + 1, false),
				c[0] !== c[3] && crossing(i, j, true),
			];
			const cut = edges.flatMap((e, k) => (e ? [k] : []));
			if (!cut.length) continue;
			let pairs = [[cut[0], cut[1]]];
			if (cut.length === 4) {
				const centre = (v[j * n + i] + v[j * n + i + 1] + v[(j + 1) * n + i + 1] + v[(j + 1) * n + i]) / 4 < 0;
				pairs = centre === c[0] ? [[0, 1], [2, 3]] : [[0, 3], [1, 2]];
			}
			for (const [ka, kb] of pairs) {
				// The corner the two edges share, or corner 0 for opposite edges; the loop keeps the inside on its left.
				const shared = [0, 1, 2, 3].find((k) => [ka, kb].every((e) => [[0, 1], [1, 2], [3, 2], [0, 3]][e].includes(k)));
				const k = shared ?? 0;
				let [from, to] = [edges[ka], edges[kb]];
				const side = (to.p[0] - from.p[0]) * (corners[k][1] - from.p[1]) - (to.p[1] - from.p[1]) * (corners[k][0] - from.p[0]);
				if (side > 0 !== c[k]) [from, to] = [to, from];
				next.set(from.id, { p: from.p, to: to.id });
			}
		}
	}
	const loops = [];
	for (const start of next.keys()) {
		if (!next.has(start)) continue;
		const loop = [];
		for (let id = start; next.has(id); ) {
			const step = next.get(id);
			next.delete(id);
			loop.push(step.p);
			id = step.to;
		}
		loops.push(thin(loop));
	}
	return loops;
}

// Loops too small to hold anything are dropped.
function loopsPath(loops) {
	return loops
		.map((loop) => {
			let area = 0;
			for (let i = 0; i < loop.length; i++) {
				const [ax, ay] = loop[i];
				const [bx, by] = loop[(i + 1) % loop.length];
				area += ax * by - bx * ay;
			}
			const pts = loop.map(([x, y]) => pt(x, y)).filter((p, i, all) => p !== all[(i + all.length - 1) % all.length]);
			return pts.length < 3 || Math.abs(area) < 1e-4 ? "" : `M${pts.join("L")}Z`;
		})
		.join("");
}

const svgFile = (viewBox, body) => `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${viewBox}">\n  ${body}\n</svg>\n`;
const traced = (parts) => `<path fill="white" d="${loopsPath(trace(parts))}"/>`;
const filledBody = (svg) => svg.replace(/<(path|circle|rect) /g, '<$1 fill="white" ').replace(/ \/>/g, "/>").replace(/> </g, ">\n  <");

mkdirSync(svgDir, { recursive: true });
const written = [];
const write = (name, text) => {
	writeFileSync(resolve(svgDir, `${name}.svg`), text);
	written.push(name);
};
for (const name of names) {
	const g = icons.glyphs[name];
	write(name, svgFile(icons.viewBox, g.filled ? filledBody(g.svg) : traced(strokeParts(g.svg, icons.strokeWidth / 2))));
}
write("plusBold", svgFile(icons.viewBox, traced(strokeParts(icons.glyphs.plus.svg, 1.5))));

// assets/ui/icons' ten too, those of several shapes traced as one; three take _solid where the
// line set has the name.
for (const file of readdirSync(assetIcons).filter((f) => f.endsWith(".svg")).sort()) {
	const text = readFileSync(resolve(assetIcons, file), "utf8");
	const stem = basename(file, ".svg");
	const name = icons.glyphs[stem] ? `${stem}_solid` : stem;
	const shapes = [...text.matchAll(/<(polygon|rect|path|circle)\b([^>]*)>/g)];
	if (shapes.length < 2) {
		write(name, text);
		continue;
	}
	const parts = shapes.map(([, tag, attrs]) => {
		if (tag === "polygon") {
			const nums = attrs.match(/points="([^"]+)"/)[1].trim().split(/[\s,]+/).map(Number);
			return polygonDistance(nums.flatMap((_, i) => (i % 2 ? [] : [[nums[i], nums[i + 1]]])));
		}
		if (tag !== "rect") throw new Error(`${file}: trace only polygons and rects`);
		const [x, y, w, h] = ["x", "y", "width", "height"].map((a) => attr(attrs, a));
		return polygonDistance([[x, y], [x + w, y], [x + w, y + h], [x, y + h]]);
	});
	write(name, svgFile(text.match(/viewBox="([^"]+)"/)[1], traced(parts)));
}
console.log(`Wrote ${written.length} icons to ${svgDir}`);
