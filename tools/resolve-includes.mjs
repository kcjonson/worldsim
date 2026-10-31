#!/usr/bin/env node
/*
 * Resolve every #include under libs/ and print a per-library include matrix.
 *
 * Each #include is resolved against the including file's directory (quoted
 * includes only) and then every library's include root, which approximates
 * the build's lookup with every library's root visible to every file. A salal
 * file sees only salal's roots, as salal's own build does, and a theme file
 * only those and its own, since theme links nothing else; so a header salal
 * generates (ui/theme/Tokens.h, theme/Tokens.h) counts as external. The
 * roots are the library directories under libs/, plus libs/ itself for
 * planet-view, which exports it; they are hardcoded here and match CMake as of
 * 2026-10-31. foundation, renderer, app and salal's ui are not in this repo:
 * they come from salal (github.com/kcjonson/salal) through FetchContent, so
 * their four roots, and their four rows, are read from a salal checkout,
 * --salal DIR, which defaults to the one FetchContent populates at
 * build/_deps/salal-src.
 * A directory under libs/ that LIBS does not list stops the script with exit 2,
 * so a new library cannot be silently ignored. The first hit decides which
 * library the include lands in; an include that resolves nowhere is counted as
 * external.
 *
 * Output, in order: an all-files matrix, a production-only matrix (test and
 * bench files excluded, see isTestFile), ambiguous resolutions, and includes
 * that cross the documented layering.
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

const LIBS = ["foundation", "geometry", "renderer", "ui", "theme", "worldgen", "planet-view", "app", "world", "assets", "sim", "world-view"];
// salal's libraries: the name each goes by here, and its directory under the checkout's libs/.
const SALAL_LIBS = new Map([
	["foundation", "foundation"],
	["renderer", "renderer"],
	["ui", "ui"],
	["app", "app"],
]);
const LIBS_ORDER = [...LIBS, "external"];
const SOURCE_EXTS = [".h", ".hpp", ".cpp", ".cc", ".inl"];
const INC = /^\s*#\s*include\s*([<"])([^>"]+)[>"]/;

// What each library may include. world and sim are the simulation side: neither may include renderer or ui.
const ALLOWED = {
	foundation: new Set(["foundation", "external"]),
	geometry: new Set(["geometry", "foundation", "external"]),
	renderer: new Set(["renderer", "foundation", "external"]),
	ui: new Set(["ui", "renderer", "foundation", "external"]),
	theme: new Set(["theme", "ui", "renderer", "foundation", "external"]),
	worldgen: new Set(["worldgen", "foundation", "external"]),
	"planet-view": new Set(["planet-view", "worldgen", "renderer", "foundation", "external"]),
	app: new Set(["app", "ui", "renderer", "foundation", "external"]),
	world: new Set(["world", "worldgen", "geometry", "foundation", "external"]),
	assets: new Set(["assets", "world", "worldgen", "geometry", "renderer", "foundation", "external"]),
	sim: new Set(["sim", "assets", "world", "worldgen", "geometry", "foundation", "external"]),
	"world-view": new Set(["world-view", "sim", "assets", "world", "worldgen", "geometry", "renderer", "foundation", "external"]),
};

const USAGE = "usage: node tools/resolve-includes.mjs [-h] [--root ROOT] [--salal DIR]";

function defaultRoot() {
	return dirname(dirname(fileURLToPath(import.meta.url)));
}

function defaultSalal(root) {
	return join(root, "build", "_deps", "salal-src");
}

// Where each library's sources live: the salal checkout for salal's four, libs/ for the rest.
function libDirs(root, salal) {
	return new Map(LIBS.map((lib) => [lib, SALAL_LIBS.has(lib) ? join(salal, "libs", SALAL_LIBS.get(lib)) : join(root, "libs", lib)]));
}

function includeRoots(dirs, root) {
	return [
		dirs.get("foundation"),
		dirs.get("geometry"),
		dirs.get("renderer"),
		dirs.get("ui"),
		dirs.get("theme"),
		dirs.get("worldgen"),
		dirs.get("planet-view"),
		join(root, "libs"), // planet-view exports libs/ so "planet-view/X.h" works
		dirs.get("app"),
		dirs.get("world"),
		dirs.get("assets"),
		dirs.get("sim"),
		dirs.get("world-view"),
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
		if (isUnder(dir, p)) return lib;
	}
	return "external";
}

// Paths in the report: salal/-prefixed for salal's libraries (wherever that checkout is), repo-relative otherwise.
function displayPath(root, salal, p) {
	return isUnder(salal, p) ? "salal/" + slashes(relative(salal, p)) : slashes(relative(root, p));
}

// Test and bench sources (*.test.cpp, *.test.h, *.bench.cpp) plus world's two test-support headers.
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
	const missing = [...SALAL_LIBS.values()].filter((dir) => !isDir(join(salal, "libs", dir)));
	if (missing.length) {
		console.error(`resolve-includes.mjs: error: no salal checkout at ${salal} (missing libs/${missing.join(", libs/")})`);
		console.error("configure the build so FetchContent populates build/_deps/salal-src, or pass --salal DIR pointing at a salal checkout");
		process.exit(2);
	}
}

function collectRows(root, salal) {
	const dirs = libDirs(root, salal);
	const roots = includeRoots(dirs, root);
	const salalRoots = [...SALAL_LIBS.keys()].map((lib) => dirs.get(lib));
	const themeRoots = [...salalRoots, dirs.get("theme")];
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
				const libRoots = SALAL_LIBS.has(lib) ? salalRoots : lib === "theme" ? themeRoots : roots;
				const cands = resolveInclude(libRoots, full, form, target);
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
	for (const s of LIBS) {
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
			console.log("  --salal DIR   salal checkout holding libs/foundation, libs/renderer, libs/ui and libs/app (default: ROOT/build/_deps/salal-src)");
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

	process.stdout.write(out.join("\n") + "\n");
}

main();
