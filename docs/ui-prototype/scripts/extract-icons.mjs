/*
 * Extract the line-icon set from the prototype's Icon.tsx into the canonical
 * docs/design/ui/design-system/icons.json, plus a rendered icons-preview.svg
 * contact sheet for the doc.
 *
 * Run from docs/ui-prototype:  node scripts/extract-icons.mjs
 *
 * The icon glyphs live only as JSX in Icon.tsx; once captured here they exist
 * independently of the prototype. None of the path data uses commas (all
 * coordinates are space-delimited), so the PATHS object splits cleanly on
 * top-level commas.
 */
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const srcPath = resolve(here, "../src/design-system/primitives/Icon/Icon.tsx");
const outJson = resolve(here, "../../design/ui/design-system/icons.json");
const outSvg = resolve(here, "../../design/ui/design-system/icons-preview.svg");

const src = readFileSync(srcPath, "utf8");

// FILLED set
const filledMatch = src.match(/const FILLED[^=]*=\s*new Set\(\[([^\]]*)\]\)/);
const filled = new Set([...filledMatch[1].matchAll(/"([^"]+)"/g)].map((m) => m[1]));

// PATHS object body
const block = src.match(/const PATHS[^{]*\{([\s\S]*?)\n\};/)[1];

const glyphs = {};
const order = [];
for (const chunk of block.split(",")) {
	const m = chunk.match(/^\s*(\w+)\s*:\s*([\s\S]*)$/);
	if (!m) continue;
	const name = m[1];
	let svg = m[2]
		.replace(/^\s*\(\s*/, "")
		.replace(/\s*\)\s*$/, "")
		.replace(/<>/g, "")
		.replace(/<\/>/g, "")
		.replace(/\s+/g, " ")
		.trim();
	glyphs[name] = { filled: filled.has(name), svg };
	order.push(name);
}

const json = {
	$comment: "GENERATED from docs/ui-prototype/src/design-system/primitives/Icon/Icon.tsx by scripts/extract-icons.mjs. The Salvage line glyphs (see count). viewBox 24x24; stroked with currentColor (round caps/joins, stroke-width 1.6) unless filled. svg is the inner markup for each glyph.",
	viewBox: "0 0 24 24",
	strokeWidth: 1.6,
	count: order.length,
	glyphs,
};

mkdirSync(dirname(outJson), { recursive: true });
writeFileSync(outJson, JSON.stringify(json, null, "\t") + "\n");

// Contact-sheet SVG: dark Salvage cells, glyph in accent, mono label.
const cols = 6;
const cellW = 120;
const cellH = 104;
const rows = Math.ceil(order.length / cols);
const W = cols * cellW;
const H = rows * cellH;
const accent = "#e8a33e";
const cells = order
	.map((name, i) => {
		const cx = (i % cols) * cellW;
		const cy = Math.floor(i / cols) * cellH;
		const g = glyphs[name];
		const paint = g.filled ? `fill="${accent}" stroke="none"` : `fill="none" stroke="${accent}" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" vector-effect="non-scaling-stroke"`;
		const glyphX = cx + cellW / 2 - 20;
		const glyphY = cy + 22;
		return `	<g>
		<g transform="translate(${glyphX},${glyphY}) scale(${40 / 24})" ${paint}>${g.svg}</g>
		<text x="${cx + cellW / 2}" y="${cy + 84}" fill="#8a8f9b" font-family="ui-monospace, monospace" font-size="10" text-anchor="middle">${name}</text>
	</g>`;
	})
	.join("\n");

const svgDoc = `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}" font-family="monospace">
	<rect width="${W}" height="${H}" fill="#0b0d12"/>
${cells}
</svg>
`;
writeFileSync(outSvg, svgDoc);

console.log(`Wrote ${order.length} glyphs (${filled.size} filled) to icons.json + icons-preview.svg`);
