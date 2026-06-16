/*
 * Extract design tokens from the prototype's tokens.css into the canonical
 * docs/design/ui/design-system/tokens.json. Deterministic: no transcription.
 *
 * Run from docs/ui-prototype:  node scripts/extract-tokens.mjs
 *
 * Colors are emitted with both the authored CSS string and normalized rgba
 * floats [0..1] (the form the future C++ Foundation::Color seed needs).
 * Aliases are resolved. Tokens whose value is a non-static CSS effect
 * (gradient, cubic-bezier, multi-layer shadow, font stack, keyword) are kept
 * as css-only and carry no numeric form.
 */
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const cssPath = resolve(here, "../src/design-system/tokens.css");
const outPath = resolve(here, "../../design/ui/design-system/tokens.json");

const css = readFileSync(cssPath, "utf8");

// Collect raw --name: value; declarations.
const raw = {};
for (const m of css.matchAll(/--([\w-]+)\s*:\s*([^;]+);/g)) {
	raw[m[1]] = m[2].trim();
}

// Resolve var(--x) references (single-level chains, no calc math).
function resolve1(value, seen = new Set()) {
	return value.replace(/var\(--([\w-]+)\)/g, (_, name) => {
		if (seen.has(name) || raw[name] === undefined) return raw[name] ?? "0";
		return resolve1(raw[name], new Set([...seen, name]));
	});
}

const r255 = (n) => Math.round((n / 255) * 10000) / 10000;
const r1 = (n) => Math.round(n * 10000) / 10000;

function parseColor(v) {
	v = v.trim();
	let m;
	if ((m = v.match(/^#([0-9a-fA-F]{3,8})$/))) {
		let hex = m[1];
		if (hex.length === 3) hex = [...hex].map((c) => c + c).join("");
		if (hex.length === 4) hex = [...hex].map((c) => c + c).join("");
		const b = hex.match(/.{2}/g).map((h) => parseInt(h, 16));
		const a = b.length === 4 ? r255(b[3]) : 1;
		return [r255(b[0]), r255(b[1]), r255(b[2]), a];
	}
	if ((m = v.match(/^rgba?\(([^)]+)\)$/))) {
		const parts = m[1].split(",").map((s) => s.trim());
		return [r255(+parts[0]), r255(+parts[1]), r255(+parts[2]), parts[3] !== undefined ? r1(+parts[3]) : 1];
	}
	return null;
}

const isColor = (v) => /^#[0-9a-fA-F]{3,8}$/.test(v) || /^rgba?\(/.test(v);
const isCssOnly = (v) => /color-mix|calc|cubic-bezier|,|\bnone\b|\bblock\b|uppercase|sans-serif|monospace|system-ui|"/.test(v);

function category(name) {
	if (/^space-/.test(name)) return "spacing";
	if (/^(r-|radius-)/.test(name)) return "radius";
	if (/^(bw|border-width)/.test(name)) return "border";
	if (/^fs-/.test(name)) return "fontSize";
	if (/^lh/.test(name)) return "lineHeight";
	if (/^ls/.test(name)) return "letterSpacing";
	if (/^(dur|ease)/.test(name)) return "motion";
	if (/^z-/.test(name)) return "zIndex";
	if (/^(font-|label-|title-)/.test(name)) return "typography";
	if (/(density|opacity)/.test(name)) return "texture";
	if (/^(panel-|corner-)/.test(name)) return "structural";
	if (/^shadow-/.test(name)) return "shadow";
	if (/^(bg-|line-|accent|data|text|status-|scrim)/.test(name)) return "color";
	return "other";
}

const num = (v) => {
	const m = v.match(/^(-?[\d.]+)(px|ms|em)?$/);
	return m ? +m[1] : null;
};

const out = {};
for (const [name, rawVal] of Object.entries(raw)) {
	const value = resolve1(rawVal);
	const cat = (out[category(name)] ??= {});
	const entry = { css: value };
	if (value !== rawVal) entry.alias = rawVal; // record it was an alias
	if (isColor(value)) {
		entry.rgba = parseColor(value);
	} else if (isCssOnly(value)) {
		entry.cssOnly = true;
	} else {
		const n = num(value);
		if (n !== null) entry.value = n;
	}
	cat[name] = entry;
}

const json = {
	$comment: "GENERATED from docs/ui-prototype/src/design-system/tokens.css by scripts/extract-tokens.mjs. Canonical Salvage token values; seed for the future C++ libs/ui/theme/Theme.h. Colors carry normalized rgba floats [0..1]. cssOnly tokens are effects with no static value (gradients, eases, shadows, font stacks).",
	...out,
};

mkdirSync(dirname(outPath), { recursive: true });
writeFileSync(outPath, JSON.stringify(json, null, "\t") + "\n");
const count = Object.values(out).reduce((a, c) => a + Object.keys(c).length, 0);

// Palette swatch sheet for tokens.md.
const colorEntries = Object.entries(out.color);
const pcols = 3;
const cellW = 250;
const cellH = 56;
const prows = Math.ceil(colorEntries.length / pcols);
const PW = pcols * cellW + 16;
const PH = prows * cellH + 16;
const swatches = colorEntries
	.map(([name, e], i) => {
		const x = 8 + (i % pcols) * cellW;
		const y = 8 + Math.floor(i / pcols) * cellH;
		return `	<g>
		<rect x="${x + 8}" y="${y + 8}" width="40" height="40" rx="2" fill="${e.css}" stroke="rgba(170,195,230,0.36)"/>
		<text x="${x + 58}" y="${y + 24}" fill="#f4eee2" font-family="ui-monospace, monospace" font-size="12">--${name}</text>
		<text x="${x + 58}" y="${y + 40}" fill="#8a8f9b" font-family="ui-monospace, monospace" font-size="11">${e.css}</text>
	</g>`;
	})
	.join("\n");
const paletteSvg = `<svg xmlns="http://www.w3.org/2000/svg" width="${PW}" height="${PH}" viewBox="0 0 ${PW} ${PH}">
	<rect width="${PW}" height="${PH}" fill="#07080b"/>
${swatches}
</svg>
`;
writeFileSync(resolve(dirname(outPath), "palette.svg"), paletteSvg);

console.log(`Wrote ${count} tokens across ${Object.keys(out).length} categories to ${outPath} (+ palette.svg)`);
