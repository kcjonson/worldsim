#!/usr/bin/env node
/*
 * Resolve every #include under libs/ and print a per-library include matrix.
 *
 * Each #include is resolved against the including file's directory (quoted
 * includes only) and then every library's include root, which approximates
 * the build's lookup with every library's root visible to every file. The
 * roots are the library directories under libs/, plus libs/ itself for
 * planet-view, which exports it; they are hardcoded here and match CMake as of
 * 2026-10-28. foundation, renderer and app are not in this repo: they come
 * from salal (github.com/kcjonson/salal) through FetchContent, so their three
 * roots, and their three rows, are read from a salal checkout, --salal DIR,
 * which defaults to the one FetchContent populates at build/_deps/salal-src.
 * A directory under libs/ that LIBS does not list stops the script with exit 2,
 * so a new library cannot be silently ignored. The first hit decides which
 * library the include lands in; an include that resolves nowhere is counted as
 * external.
 *
 * Output, in order: an all-files matrix, a production-only matrix (test and
 * bench files excluded, see isTestFile), ambiguous resolutions, includes that
 * cross the documented layering, the engine -> ui coupling surface, and what
 * engine's app/ (the bridge between worldsim's ui and salal's app, until salal
 * has a ui of its own) includes from libs/.
 * Cells are counts of #include lines; self-includes sit on the diagonal.
 *
 * The allowed edges follow the per-library "Depends on" lists in
 * docs/technical/monorepo-structure.md; planet-view's row is inferred, since
 * that doc predates it. Violations are reported, not enforced: the exit code
 * is 0 either way.
 *
 * Usage: node tools/resolve-includes.mjs [--root ROOT] [--salal DIR]
 */
import { readdirSync, readFileSync, statSync } from "node:fs";
import { basename, dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const LIBS = ["foundation", "geometry", "renderer", "ui", "world", "planet-view", "game-systems", "app", "engine"];
const SALAL_LIBS = new Set(["foundation", "renderer", "app"]);
const LIBS_ORDER = [...LIBS, "engine(assets)", "external"];
const SOURCE_EXTS = [".h", ".hpp", ".cpp", ".cc", ".inl"];
const INC = /^\s*#\s*include\s*([<"])([^>"]+)[>"]/;

// What each library may include. engine and engine(assets) have no entry: they may include anything.
const ALLOWED = {
	foundation: new Set(["foundation", "external"]),
	geometry: new Set(["geometry", "foundation", "external"]),
	renderer: new Set(["renderer", "foundation", "external"]),
	ui: new Set(["ui", "renderer", "foundation", "external"]),
	world: new Set(["world", "renderer", "foundation", "external"]),
	"planet-view": new Set(["planet-view", "world", "renderer", "foundation", "external"]),
	"game-systems": new Set(["game-systems", "world", "renderer", "foundation", "external"]),
	app: new Set(["app", "renderer", "foundation", "external"]),
};

const USAGE = "usage: node tools/resolve-includes.mjs [-h] [--root ROOT] [--salal DIR]";

function defaultRoot() {
	return dirname(dirname(fileURLToPath(import.meta.url)));
}

function defaultSalal(root) {
	return join(root, "build", "_deps", "salal-src");
}

// Where each library's sources live: the salal checkout for foundation, renderer and app, libs/ for the rest.
function libDirs(root, salal) {
	return new Map(LIBS.map((lib) => [lib, SALAL_LIBS.has(lib) ? join(salal, "libs", lib) : join(root, "libs", lib)]));
}

function includeRoots(dirs, root) {
	return [
		dirs.get("foundation"),
		dirs.get("geometry"),
		dirs.get("renderer"),
		dirs.get("ui"),
		dirs.get("world"),
		dirs.get("planet-view"),
		join(root, "libs"), // planet-view exports libs/ so "planet-view/X.h" works
		dirs.get("game-systems"),
		dirs.get("app"),
		dirs.get("engine"), // also covers "assets/X.h" -> libs/engine/assets/X.h
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

function byName(a, b) {
	return a < b ? -1 : a > b ? 1 : 0;
}

function isUnder(dir, p) {
	const rel = relative(dir, p);
	return rel !== "" && !rel.startsWith("..") && !rel.includes(":");
}

function libOf(dirs, p) {
	for (const [lib, dir] of dirs) {
		if (!isUnder(dir, p)) continue;
		if (lib === "engine" && isUnder(join(dir, "assets"), p)) return "engine(assets)";
		return lib;
	}
	return "external";
}

// Paths in the report: salal/-prefixed for salal's libraries (wherever that checkout is), repo-relative otherwise.
function displayPath(root, salal, p) {
	return isUnder(salal, p) ? "salal/" + slashes(relative(salal, p)) : slashes(relative(root, p));
}

// Test and bench sources (*.test.cpp, *.test.h, *.bench.cpp) plus the two engine test-support headers.
function isTestFile(p) {
	const name = basename(p);
	return name.includes(".test.") || name.includes(".bench.") || name.endsWith("TestWorld.h") || name.endsWith("TestSupport.h");
}

function resolveInclude(roots, srcFile, form, target) {
	const cands = [];
	if (form === '"') {
		const p = resolve(dirname(srcFile), target);
		if (isFile(p)) cands.push(p);
	}
	for (const incRoot of roots) {
		const p = resolve(incRoot, target);
		if (isFile(p) && !cands.includes(p)) cands.push(p);
	}
	return cands;
}

// Files of a directory first, by name, then each subdirectory in turn, so the order is the same on every filesystem.
function* walk(dir) {
	let entries;
	try {
		entries = readdirSync(dir, { withFileTypes: true });
	} catch {
		return;
	}
	entries.sort((a, b) => byName(a.name, b.name));
	const dirs = [];
	for (const e of entries) {
		if (e.isDirectory()) dirs.push(e.name);
		else yield join(dir, e.name);
	}
	for (const d of dirs) yield* walk(join(dir, d));
}

// A libs/ directory this script does not know would otherwise be silently left out of every matrix,
// and a copy of a salal library under libs/ would be silently passed over in favour of salal's.
function checkLibs(root) {
	const known = new Set(LIBS);
	const dirs = readdirSync(join(root, "libs"), { withFileTypes: true })
		.filter((e) => e.isDirectory())
		.map((e) => e.name)
		.sort(byName);
	const unknown = dirs.filter((d) => !known.has(d));
	if (unknown.length) {
		console.error(`resolve-includes.mjs: error: unknown directories under libs/: ${unknown.join(", ")}`);
		console.error("add each to LIBS, includeRoots and ALLOWED, then re-run");
		process.exit(2);
	}
	const stale = dirs.filter((d) => SALAL_LIBS.has(d));
	if (stale.length) {
		console.error(`resolve-includes.mjs: error: libs/${stale.join(", libs/")} would be ignored: those libraries are read from the salal checkout (--salal)`);
		process.exit(2);
	}
}

function checkSalal(salal) {
	const missing = [...SALAL_LIBS].filter((lib) => !isDir(join(salal, "libs", lib)));
	if (missing.length) {
		console.error(`resolve-includes.mjs: error: no salal checkout at ${salal} (missing libs/${missing.join(", libs/")})`);
		console.error("configure the build so FetchContent populates build/_deps/salal-src, or pass --salal DIR pointing at a salal checkout");
		process.exit(2);
	}
}

function collectRows(root, salal) {
	const dirs = libDirs(root, salal);
	const roots = includeRoots(dirs, root);
	const rows = [];
	for (const lib of LIBS) {
		for (const full of walk(dirs.get(lib))) {
			if (!SOURCE_EXTS.some((ext) => full.endsWith(ext))) continue;
			const rel = displayPath(root, salal, full);
			const srclib = libOf(dirs, full);
			const test = isTestFile(full);
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
				let resolved = "";
				let ambiguous = false;
				if (cands.length) {
					dst = libOf(dirs, cands[0]);
					resolved = displayPath(root, salal, cands[0]);
					ambiguous = new Set(cands.map((c) => libOf(dirs, c))).size > 1;
				}
				rows.push({ srclib, rel, line: i + 1, form, target, dst, resolved, ambiguous, test });
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

const COL = Math.max(...LIBS_ORDER.map((c) => c.length)) + 1;

function printMatrix(out, m, title) {
	out.push("", `=== ${title} ===`);
	out.push("src\\dst".padEnd(16) + LIBS_ORDER.map((c) => c.padStart(COL)).join(""));
	for (const s of [...LIBS, "engine(assets)"]) {
		const row = m.get(s);
		if (!row) continue;
		out.push(s.padEnd(16) + LIBS_ORDER.map((c) => String(row.get(c) ?? 0).padStart(COL)).join(""));
	}
}

function spelled(r) {
	return `#include ${r.form}${r.target}${r.form === "<" ? ">" : '"'}`;
}

function byFileLine(a, b) {
	return byName(a.rel, b.rel) || a.line - b.line;
}

function byCountDesc(a, b) {
	return b[1] - a[1] || byName(a[0], b[0]);
}

function fail(msg) {
	console.error(USAGE);
	console.error(`resolve-includes.mjs: error: ${msg}`);
	process.exit(2);
}

function parseArgs(argv) {
	let root = defaultRoot();
	let salal;
	for (let i = 0; i < argv.length; i++) {
		const a = argv[i];
		if (a === "-h" || a === "--help") {
			console.log(USAGE);
			console.log("\nResolve every #include under libs/ and print a per-library include matrix.");
			console.log("\n  --root ROOT   repo root containing libs/ (default: the repo this script lives in)");
			console.log("  --salal DIR   salal checkout holding libs/foundation, libs/renderer and libs/app (default: ROOT/build/_deps/salal-src)");
			process.exit(0);
		} else if (a === "--root") {
			if (i + 1 >= argv.length) fail("argument --root: expected one argument");
			root = argv[++i];
		} else if (a.startsWith("--root=")) {
			root = a.slice("--root=".length);
		} else if (a === "--salal") {
			if (i + 1 >= argv.length) fail("argument --salal: expected one argument");
			salal = argv[++i];
		} else if (a.startsWith("--salal=")) {
			salal = a.slice("--salal=".length);
		} else {
			fail(`unrecognized arguments: ${a}`);
		}
	}
	root = resolve(root);
	return { root, salal: resolve(salal ?? defaultSalal(root)) };
}

function main() {
	const { root, salal } = parseArgs(process.argv.slice(2));
	if (!isDir(join(root, "libs"))) fail(`no libs/ directory under ${root}`);
	checkLibs(root);
	checkSalal(salal);

	const rows = collectRows(root, salal);
	const out = [];

	const all = new Map();
	const production = new Map();
	for (const r of rows) {
		tallyCell(all, r.srclib, r.dst);
		if (!r.test) tallyCell(production, r.srclib, r.dst);
	}
	printMatrix(out, all, "INCLUDE MATRIX, all files (counts of #include lines; self-includes on the diagonal)");
	printMatrix(out, production, "INCLUDE MATRIX, production (test and bench files excluded)");

	const ambiguities = rows.filter((r) => r.ambiguous).sort(byFileLine);
	out.push("", "=== AMBIGUOUS resolutions (header name exists in >1 lib) ===");
	for (const r of ambiguities) out.push(`  ${r.rel}:${r.line}  ${spelled(r)}  -> picked ${r.dst} (${r.resolved})`);
	if (!ambiguities.length) out.push("  none");

	out.push("", "=== VIOLATIONS (includes that cross the documented layering; reported, not enforced) ===");
	for (const [s, allowed] of Object.entries(ALLOWED)) {
		const v = rows.filter((r) => r.srclib === s && !allowed.has(r.dst)).sort(byFileLine);
		out.push("", `-- ${s}: ${v.length} violating include(s)`);
		for (const r of v) out.push(`  ${r.rel}:${r.line}  ${spelled(r)}  -> ${r.resolved} [${r.dst}]`);
	}

	out.push("", "=== ENGINE -> UI includes (allowed; coupling surface) ===");
	const eu = rows.filter((r) => (r.srclib === "engine" || r.srclib === "engine(assets)") && r.dst === "ui");
	const byHdr = new Map();
	const byDir = new Map();
	for (const r of eu) {
		tally(byHdr, r.resolved);
		tally(byDir, r.rel.split("/").slice(0, 3).join("/"));
	}
	out.push(`total engine->ui include lines: ${eu.length} across ${new Set(eu.map((r) => r.rel)).size} engine files`);
	out.push("", "by engine subdir:");
	for (const [d, c] of [...byDir].sort(byCountDesc)) out.push(`  ${d.padEnd(45)} ${c}`);
	out.push("", "by ui header (which ui headers engine pulls in):");
	for (const [h, c] of [...byHdr].sort(byCountDesc)) out.push(`  ${h.padEnd(50)} ${c}`);
	out.push("", "engine files that include ui (file:line -> header):");
	for (const r of [...eu].sort(byFileLine)) out.push(`  ${r.rel}:${r.line} -> ${r.resolved}`);

	out.push("", "=== ENGINE app/ (the ui bridge onto salal's app): includes that land in libs/ ===");
	for (const r of [...rows].sort(byFileLine)) {
		if (r.srclib === "engine" && r.rel.split("/")[2] === "app" && r.dst !== "external") {
			out.push(`  ${r.rel}:${r.line} -> ${r.resolved} [${r.dst}]`);
		}
	}

	process.stdout.write(out.join("\n") + "\n");
}

main();
