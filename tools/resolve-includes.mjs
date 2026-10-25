#!/usr/bin/env node
/*
 * Resolve every #include under libs/ and print a per-library include matrix.
 *
 * Each #include is resolved against each library's declared include root plus
 * the including file's directory (quoted includes only), the way the build
 * would find it. Roots are the library directories under libs/, plus libs/
 * itself for planet-view, which exports it. The first hit decides which
 * library the include lands in; an include that resolves nowhere is counted
 * as external. Production files and test/bench files are tallied separately.
 *
 * Output, in order: the matrix for all files, the matrix for production files
 * only (*.test.cpp and *.bench.cpp excluded), ambiguous resolutions, includes
 * that break the documented layering, and the engine -> ui coupling surface.
 * Cells are counts of #include lines; self-includes sit on the diagonal.
 *
 * Usage: node tools/resolve-includes.mjs [--root PATH]
 */
import { readdirSync, readFileSync, statSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const LIBS = ["foundation", "geometry", "renderer", "ui", "world", "planet-view", "game-systems", "engine"];
const LIBS_ORDER = [...LIBS, "engine(assets)", "external"];
const SOURCE_EXTS = [".h", ".hpp", ".cpp", ".cc", ".inl"];
const INC = /^\s*#\s*include\s*([<"])([^>"]+)[>"]/;

const ALLOWED = {
	foundation: new Set(["foundation", "external"]),
	geometry: new Set(["geometry", "foundation", "external"]),
	renderer: new Set(["renderer", "foundation", "external"]),
	ui: new Set(["ui", "renderer", "foundation", "external"]),
	world: new Set(["world", "renderer", "foundation", "external"]),
	"planet-view": new Set(["planet-view", "world", "renderer", "foundation", "external"]),
	"game-systems": new Set(["game-systems", "world", "renderer", "foundation", "external"]),
	engine: new Set(LIBS_ORDER),
	"engine(assets)": new Set(LIBS_ORDER),
};

const USAGE = "usage: node tools/resolve-includes.mjs [-h] [--root ROOT]";

function defaultRoot() {
	return dirname(dirname(fileURLToPath(import.meta.url)));
}

function includeRoots(root) {
	const libs = join(root, "libs");
	return [
		["foundation", join(libs, "foundation")],
		["geometry", join(libs, "geometry")],
		["renderer", join(libs, "renderer")],
		["ui", join(libs, "ui")],
		["world", join(libs, "world")],
		["planet-view", join(libs, "planet-view")],
		["planet-view", libs], // planet-view exports libs/ so "planet-view/X.h" works
		["game-systems", join(libs, "game-systems")],
		["engine", join(libs, "engine")], // also covers "assets/X.h" -> libs/engine/assets/X.h
	];
}

function isFile(p) {
	try {
		return statSync(p, { throwIfNoEntry: false })?.isFile() ?? false;
	} catch {
		return false;
	}
}

function isDir(p) {
	try {
		return statSync(p, { throwIfNoEntry: false })?.isDirectory() ?? false;
	} catch {
		return false;
	}
}

function slashes(p) {
	return p.replaceAll("\\", "/");
}

function libOf(root, p) {
	const rel = slashes(relative(join(root, "libs"), p));
	const top = rel.split("/")[0];
	if (top === "engine" && rel.startsWith("engine/assets/")) return "engine(assets)";
	return top;
}

function resolveInclude(roots, srcFile, form, target) {
	const cands = [];
	if (form === '"') {
		const p = resolve(dirname(srcFile), target);
		if (isFile(p)) cands.push(p);
	}
	for (const [, incRoot] of roots) {
		const p = resolve(incRoot, target);
		if (isFile(p) && !cands.includes(p)) cands.push(p);
	}
	return cands;
}

// Files of a directory first, in directory order, then each subdirectory in turn.
function* walk(dir) {
	let entries;
	try {
		entries = readdirSync(dir, { withFileTypes: true });
	} catch {
		return;
	}
	const dirs = [];
	for (const e of entries) {
		if (e.isDirectory()) dirs.push(e.name);
		else yield join(dir, e.name);
	}
	for (const d of dirs) yield* walk(join(dir, d));
}

function collectRows(root) {
	const roots = includeRoots(root);
	const rows = [];
	for (const lib of LIBS) {
		for (const full of walk(join(root, "libs", lib))) {
			if (!SOURCE_EXTS.some((ext) => full.endsWith(ext))) continue;
			const rel = slashes(relative(root, full));
			const srclib = libOf(root, full);
			let lines;
			try {
				lines = readFileSync(full, "utf8").split(/\r\n|\r|\n/);
			} catch (e) {
				console.error("ERR", full, e.message);
				continue;
			}
			for (let i = 0; i < lines.length; i++) {
				const m = INC.exec(lines[i]);
				if (!m) continue;
				const [, form, target] = m;
				const cands = resolveInclude(roots, full, form, target);
				let dst = "external";
				let rres = "";
				let amb = false;
				if (cands.length) {
					dst = libOf(root, cands[0]);
					rres = slashes(relative(root, cands[0]));
					amb = new Set(cands.map((c) => libOf(root, c))).size > 1;
				}
				rows.push({ srclib, rel, line: i + 1, form, target, dst, rres, amb });
			}
		}
	}
	return rows;
}

function tally(map, key) {
	map.set(key, (map.get(key) ?? 0) + 1);
}

function tallyCell(matrix, src, dst) {
	let row = matrix.get(src);
	if (!row) matrix.set(src, (row = new Map()));
	tally(row, dst);
}

function printMatrix(out, m, title) {
	out.push("", `=== ${title} ===`);
	out.push("src\\dst".padEnd(16) + LIBS_ORDER.map((c) => c.slice(0, 12).padStart(13)).join(""));
	for (const s of [...LIBS, "engine(assets)"]) {
		const row = m.get(s);
		if (!row) continue;
		out.push(s.padEnd(16) + LIBS_ORDER.map((c) => String(row.get(c) ?? 0).padStart(13)).join(""));
	}
}

function byFileLine(a, b) {
	if (a.rel !== b.rel) return a.rel < b.rel ? -1 : 1;
	return a.line - b.line;
}

function byCountDesc(a, b) {
	return b[1] - a[1];
}

function fail(msg) {
	console.error(USAGE);
	console.error(`resolve-includes.mjs: error: ${msg}`);
	process.exit(2);
}

function parseArgs(argv) {
	let root = defaultRoot();
	for (let i = 0; i < argv.length; i++) {
		const a = argv[i];
		if (a === "-h" || a === "--help") {
			console.log(USAGE);
			console.log("\nResolve every #include under libs/ and print a per-library include matrix.");
			console.log("\n  --root ROOT  repo root containing libs/ (default: the repo this script lives in)");
			process.exit(0);
		} else if (a === "--root") {
			if (i + 1 >= argv.length) fail("argument --root: expected one argument");
			root = argv[++i];
		} else if (a.startsWith("--root=")) {
			root = a.slice("--root=".length);
		} else {
			fail(`unrecognized arguments: ${a}`);
		}
	}
	return root;
}

function main() {
	const root = resolve(parseArgs(process.argv.slice(2)));
	if (!isDir(join(root, "libs"))) fail(`no libs/ directory under ${root}`);

	const rows = collectRows(root);
	const out = [];

	const mat = new Map();
	const matProd = new Map();
	for (const r of rows) {
		tallyCell(mat, r.srclib, r.dst);
		if (!r.rel.includes(".test.") && !r.rel.includes(".bench.")) tallyCell(matProd, r.srclib, r.dst);
	}
	printMatrix(out, mat, "INCLUDE MATRIX, all files (counts of #include lines; self-includes on the diagonal)");
	printMatrix(out, matProd, "INCLUDE MATRIX, production only (excluding *.test.cpp / *.bench.cpp)");

	const ambs = rows.filter((r) => r.amb);
	out.push("", "=== AMBIGUOUS resolutions (header name exists in >1 lib) ===");
	for (const r of ambs) out.push(`  ${r.rel}:${r.line}  ${r.form}${r.target}  -> picked ${r.dst} (${r.rres})`);
	if (!ambs.length) out.push("  none");

	out.push("", "=== VIOLATIONS (includes that cross the documented layering) ===");
	for (const s of ["foundation", "geometry", "renderer", "ui", "world", "planet-view", "game-systems"]) {
		const v = rows.filter((r) => r.srclib === s && !ALLOWED[s].has(r.dst));
		out.push("", `-- ${s}: ${v.length} violating include(s)`);
		for (const r of v) {
			const close = r.form === "<" ? ">" : '"';
			out.push(`  ${r.rel}:${r.line}  #include ${r.form}${r.target}${close}  -> ${r.rres} [${r.dst}]`);
		}
	}

	out.push("", "=== ENGINE -> UI includes (allowed; coupling surface) ===");
	const eu = rows.filter((r) => (r.srclib === "engine" || r.srclib === "engine(assets)") && r.dst === "ui");
	const byHdr = new Map();
	const byDir = new Map();
	for (const r of eu) {
		tally(byHdr, r.rres);
		tally(byDir, r.rel.split("/").slice(0, 3).join("/"));
	}
	out.push(`total engine->ui include lines: ${eu.length} across ${new Set(eu.map((r) => r.rel)).size} engine files`);
	out.push("", "by engine subdir:");
	for (const [d, c] of [...byDir].sort(byCountDesc)) out.push(`  ${d.padEnd(45)} ${c}`);
	out.push("", "by ui header (which ui headers engine pulls in):");
	for (const [h, c] of [...byHdr].sort(byCountDesc)) out.push(`  ${h.padEnd(50)} ${c}`);
	out.push("", "engine files that include ui (file:line -> header):");
	for (const r of [...eu].sort(byFileLine)) out.push(`  ${r.rel}:${r.line} -> ${r.rres}`);

	out.push("", "=== ENGINE generic slice (application/, scene/, input/, clipboard/): non-self includes ===");
	const generic = new Set(["application", "scene", "input", "clipboard"]);
	for (const r of [...rows].sort(byFileLine)) {
		if (r.srclib === "engine" && generic.has(r.rel.split("/")[2]) && r.dst !== "external") {
			out.push(`  ${r.rel}:${r.line} -> ${r.rres} [${r.dst}]`);
		}
	}

	process.stdout.write(out.join("\n") + "\n");
}

main();
