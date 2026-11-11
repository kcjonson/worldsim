#!/usr/bin/env node
// Test-result caching for the Tests workflow: skip a ctest entry that already passed with exactly the
// same inputs. docs/testing/README.md ("CI test selection") is the user-facing description.
//
//   node scripts/ci/test-select.mjs query  --build-dir build
//       Before configure: asks CMake's file API for the codemodel, toolchains, cmakeFiles and cache.
//   node scripts/ci/test-select.mjs plan   --build-dir build [--markers DIR] [--exclude REGEX]
//                                          [--config CFG] [--rev REV] [--ignore-markers] [--explain] [--json]
//       After build: keys every ctest entry, decides run or skip, writes <build>/test-select/plan.json.
//   node scripts/ci/test-select.mjs record --build-dir build --markers DIR --junit FILE
//       After ctest: rewrites DIR to hold one marker per test that passed now or was skipped on a
//       valid marker, and nothing else. Fails if a test planned to run has no result.

import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const kFileApiQueries = ["codemodel-v2", "toolchains-v1", "cmakeFiles-v1", "cache-v2"];

// Top-level entries that feed neither the build nor a test. Anything else at the top level is in
// every key unless a target or a test's data dirs claim it, so a new file or dir reruns everything.
const kIgnoredTopLevel = new Set([
	".claude",
	".clang-format", // code-quality.yml only
	".clang-tidy",
	".gitignore",
	".vscode",
	"LICENSE",
	"docs",
	"perf-results",
	"scripts", // scripts/ci is added back below, and a CMake helper here through cmakeFiles
	".github", // tests.yml and .github/actions are added back below
]);
const kIgnoredTopLevelSuffix = [".md"];
const kGithubInputs = [".github/workflows/tests.yml", ".github/actions"];

// Properties that change only when or how long ctest runs a test, never what it runs.
const kSchedulingProperties = new Set(["TIMEOUT", "PROCESSORS", "COST", "RESOURCE_LOCK", "RUN_SERIAL", "LABELS"]);

const kKeyLength = 20;
const kSetFile = "set.json";
const kMaxMarkerAgeHours = 48;
// Input lines whose second-to-last word is a path and last word its object id.
const kPathLines = new Set(["global", "target", "source", "root", "include", "cmake", "data"]);

function fail(message) {
	console.error(`test-select: ${message}`);
	process.exit(2);
}

function parseArgs(argv) {
	const [command, ...rest] = argv;
	const flags = new Set(["--ignore-markers", "--explain", "--json"]);
	const opts = {};
	for (let i = 0; i < rest.length; i++) {
		const arg = rest[i];
		if (flags.has(arg)) {
			opts[arg.slice(2)] = true;
		} else if (arg.startsWith("--") && i + 1 < rest.length) {
			opts[arg.slice(2)] = rest[++i];
		} else {
			fail(`unexpected argument ${arg}`);
		}
	}
	return { command, opts };
}

const slash = (p) => p.replaceAll("\\", "/");
const sameCase = (p) => (process.platform === "win32" ? p.toLowerCase() : p);
const sha256 = (text) => createHash("sha256").update(text).digest("hex");

function isUnder(child, parent) {
	const c = sameCase(slash(child));
	const p = sameCase(slash(parent)).replace(/\/$/, "");
	return c === p || c.startsWith(`${p}/`);
}

// Repo-relative form of a path CMake reported, or null when it is outside the repo or in the build
// dir (CI's build/ is inside the checkout: build/_deps/salal-src, vcpkg_installed).
function repoRelative(p, sourceRoot, buildRoot) {
	const abs = path.isAbsolute(p) ? p : path.join(sourceRoot, p);
	if (!isUnder(abs, sourceRoot) || isUnder(abs, buildRoot)) return null;
	return slash(path.relative(sourceRoot, abs)) || ".";
}

function run(cmd, args, opts = {}) {
	const r = spawnSync(cmd, args, { encoding: "utf8", maxBuffer: 256 * 1024 * 1024, ...opts });
	if (r.error) fail(`${cmd} ${args.join(" ")}: ${r.error.message}`);
	if (r.status !== 0) fail(`${cmd} ${args.join(" ")} exited ${r.status}\n${r.stderr}`);
	return r.stdout;
}

// --- CMake file API ------------------------------------------------------------------------------

function writeQueries(buildDir) {
	const dir = path.join(buildDir, ".cmake", "api", "v1", "query");
	fs.mkdirSync(dir, { recursive: true });
	for (const q of kFileApiQueries) fs.writeFileSync(path.join(dir, q), "");
}

function readReply(buildDir) {
	const dir = path.join(buildDir, ".cmake", "api", "v1", "reply");
	if (!fs.existsSync(dir)) fail(`no file API reply in ${dir}; run "query" before configuring`);
	const index = fs.readdirSync(dir).filter((f) => f.startsWith("index-")).sort().at(-1);
	if (!index) fail(`no file API index in ${dir}`);
	const read = (f) => JSON.parse(fs.readFileSync(path.join(dir, f), "utf8"));
	const objects = {};
	for (const o of read(index).objects) objects[o.kind] = read(o.jsonFile);
	for (const q of kFileApiQueries) {
		const kind = q.replace(/-v\d+$/, "");
		if (!objects[kind]) fail(`the file API reply has no ${kind}; reconfigure after "query"`);
	}
	return { dir, read, objects };
}

function loadGraph(buildDir, config) {
	const { read, objects } = readReply(buildDir);
	const codemodel = objects.codemodel;
	const sourceRoot = codemodel.paths.source;
	const buildRoot = codemodel.paths.build;
	const configs = codemodel.configurations;
	const cfg = config ? configs.find((c) => c.name === config) : configs.length === 1 ? configs[0] : null;
	if (!cfg) fail(`pick a configuration with --config (${configs.map((c) => c.name).join(", ")})`);

	const tracked = (p) => repoRelative(p, sourceRoot, buildRoot);
	const targets = new Map();
	for (const t of cfg.targets) {
		const json = read(t.jsonFile);
		const dir = tracked(json.paths.source);
		// A source outside its target's dir is keyed by its own dir, which holds the headers it
		// quote-includes. The root's tree would be the whole repo, so a root target's sources go too.
		const sourceDirs = new Set();
		for (const s of json.sources ?? []) {
			if (s.isGenerated) continue;
			const rel = tracked(s.path);
			if (rel && (!dir || dir === "." || !isUnder(rel, dir))) sourceDirs.add(path.posix.dirname(rel));
		}
		const includes = new Set();
		for (const group of json.compileGroups ?? []) {
			for (const inc of group.includes ?? []) {
				const rel = tracked(inc.path);
				if (rel) includes.add(rel);
			}
		}
		targets.set(t.id, {
			id: t.id,
			name: t.name,
			type: json.type,
			dir,
			sourceDirs: [...sourceDirs].sort(),
			includes: [...includes].sort(),
			deps: (json.dependencies ?? []).map((d) => d.id),
			artifacts: (json.artifacts ?? []).map((a) => sameCase(slash(path.resolve(buildRoot, a.path)))),
		});
	}

	const toolchains = objects.toolchains.toolchains
		.filter((t) => t.compiler?.id)
		.map((t) => `${t.language} ${t.compiler.id} ${t.compiler.version ?? ""}`.trim())
		.sort();

	const cmakeInputs = objects.cmakeFiles.inputs
		.filter((i) => !i.isExternal && !i.isCMake && !i.isGenerated)
		.map((i) => tracked(i.path))
		.filter(Boolean);

	return { sourceRoot, buildRoot, tracked, config: cfg.name, targets, toolchains, cmakeInputs };
}

function closure(targets, rootId) {
	const seen = new Set();
	const stack = [rootId];
	while (stack.length) {
		const id = stack.pop();
		if (seen.has(id) || !targets.has(id)) continue;
		seen.add(id);
		stack.push(...targets.get(id).deps);
	}
	return [...seen].map((id) => targets.get(id));
}

// --- ctest ---------------------------------------------------------------------------------------

function listTests(buildDir, exclude, config) {
	const args = ["--show-only=json-v1"];
	if (exclude) args.push("-E", exclude);
	if (config) args.push("-C", config);
	return JSON.parse(run("ctest", args, { cwd: buildDir })).tests;
}

// --- git -----------------------------------------------------------------------------------------

// Every path in the rev's tree, trees and submodule gitlinks included, to its object id. A path that
// isn't here is really absent at the rev.
function gitTree(sourceRoot, rev) {
	const objects = new Map([[".", run("git", ["rev-parse", `${rev}^{tree}`], { cwd: sourceRoot }).trim()]]);
	const topLevel = [];
	for (const entry of run("git", ["ls-tree", "-r", "-t", "-z", rev], { cwd: sourceRoot }).split("\0")) {
		if (!entry) continue;
		const tab = entry.indexOf("\t");
		const name = entry.slice(tab + 1);
		objects.set(name, entry.slice(0, tab).split(" ")[2]);
		if (!name.includes("/")) topLevel.push(name);
	}
	return { objects, topLevel, sha: (p) => objects.get(p) ?? "missing" };
}

// --- keys ----------------------------------------------------------------------------------------

function normaliseValue(value, graph) {
	const text = typeof value === "string" ? value : JSON.stringify(value);
	const build = slash(graph.buildRoot);
	const source = slash(graph.sourceRoot);
	const flags = process.platform === "win32" ? "gi" : "g";
	const escape = (s) => s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
	return slash(text).replace(new RegExp(escape(build), flags), "<build>").replace(new RegExp(escape(source), flags), "<source>");
}

// dir holds p: the root holds everything.
const holds = (dir, p) => dir === "." || isUnder(p, dir);
const topOf = (p) => p.split("/")[0];

function computeKeys(graph, tests, allTests, rev) {
	const { sourceRoot, targets, tracked } = graph;
	const scriptDir = tracked(path.dirname(fileURLToPath(import.meta.url)));
	if (!scriptDir) fail("the script must live in the repo it selects tests for");
	const tree = gitTree(sourceRoot, rev);

	const dataDirsOf = (test) =>
		(test.properties ?? [])
			.filter((p) => p.name === "REQUIRED_FILES")
			.flatMap((p) => p.value)
			.map((f) => tracked(f) ?? fail(`${test.name}: REQUIRED_FILES entry ${f} isn't in the source tree, so no key can see it`));

	const targetDirs = [...targets.values()].map((t) => t.dir).filter((d) => d && d !== ".");
	const byArtifact = new Map();
	for (const t of targets.values()) for (const a of t.artifacts) byArtifact.set(a, t);

	// Top-level names a target or any test's data dirs account for; they leave the global part. Read
	// from every test, not just the planned ones, so --exclude doesn't change a key.
	const claimed = new Set([...targetDirs, ...allTests.flatMap(dataDirsOf)].map(topOf));
	const keyedTopLevel = tree.topLevel.filter(
		(name) => !kIgnoredTopLevel.has(name) && !kIgnoredTopLevelSuffix.some((s) => name.endsWith(s)),
	);

	const globalPaths = keyedTopLevel.filter((name) => !claimed.has(name));
	globalPaths.push(...kGithubInputs, scriptDir);
	// A CMake file that isn't a CMakeLists.txt (an included helper, wherever it lives) and a
	// CMakeLists.txt with no target at or below its dir can shape any target.
	const cmakeLists = [];
	for (const input of graph.cmakeInputs) {
		if (path.posix.basename(input) !== "CMakeLists.txt") globalPaths.push(input);
		else {
			const dir = path.posix.dirname(input);
			if ([...targets.values()].some((t) => t.dir && holds(dir, t.dir))) cmakeLists.push(input);
			else globalPaths.push(input);
		}
	}

	const perTest = tests.map((test) => {
		const dataDirs = [...new Set(dataDirsOf(test))].sort();
		for (const d of dataDirs) {
			if (!tree.objects.has(d)) fail(`${test.name}: data dir ${d} isn't tracked at ${rev} (gitignored, or spelled with another case?)`);
		}
		if (!test.command?.length) fail(`${test.name}: ctest reports no command; build the tests before planning`);
		const exe = sameCase(slash(path.resolve(graph.buildRoot, test.command[0])));
		const target = byArtifact.get(exe);
		if (!target) fail(`${test.name}: its command ${test.command[0]} is no CMake target's artifact, so its inputs are unknown; add_test(COMMAND <target>)`);
		const members = closure(targets, target.id);
		const ownDirs = members.map((m) => m.dir).filter((d) => d && d !== ".");
		// An include root wider than every closure target's own dir (planet-view exports libs/) reaches
		// headers of libraries the exe never links, so it is keyed whole.
		const includeDirs = [...new Set(members.flatMap((m) => m.includes))]
			.filter((inc) => !ownDirs.some((d) => isUnder(inc, d)))
			.sort();
		// A CMakeLists.txt above a member's dir (the member's own is in its tree) configures it.
		const ancestorLists = cmakeLists.filter((lists) => {
			const dir = path.posix.dirname(lists);
			return !globalPaths.includes(lists) && ownDirs.some((d) => d !== dir && holds(dir, d));
		});
		// A root-dir target's inputs can be anywhere, so every keyed top-level entry goes in.
		const rootEntries = members.some((m) => m.dir === ".") ? keyedTopLevel.filter((n) => claimed.has(n)) : [];
		const runProps = (test.properties ?? [])
			.filter((p) => !kSchedulingProperties.has(p.name) && p.name !== "REQUIRED_FILES")
			.sort((a, b) => a.name.localeCompare(b.name));
		return { test, target, members, dataDirs, runProps, includeDirs, ancestorLists, rootEntries };
	});

	const env = [
		`env os ${process.platform}-${process.arch}${process.env.ImageOS ? ` ${process.env.ImageOS}` : ""}`,
		...graph.toolchains.map((t) => `env compiler ${t}`),
		`env config ${graph.config}`,
	];
	const global = [...new Set(globalPaths)].sort().map((p) => `global ${p} ${tree.sha(p)}`);

	return perTest.map(({ test, target, members, dataDirs, runProps, includeDirs, ancestorLists, rootEntries }) => {
		const inputs = [...env, ...global];
		inputs.push(`exe ${target.name}`);
		inputs.push(`args ${normaliseValue(test.command.slice(1), graph)}`);
		for (const p of runProps) inputs.push(`property ${p.name} ${normaliseValue(p.value, graph)}`);
		for (const m of [...members].sort((a, b) => a.name.localeCompare(b.name))) {
			if (!m.dir) continue; // outside the repo or in the build dir: salal, keyed by its tag in the root CMakeLists.txt
			inputs.push(m.dir === "." ? `target ${m.name} . see-root-lines` : `target ${m.name} ${m.dir} ${tree.sha(m.dir)}`);
			for (const s of m.sourceDirs) inputs.push(`source ${m.name} ${s} ${tree.sha(s)}`);
		}
		for (const e of rootEntries) inputs.push(`root ${e} ${tree.sha(e)}`);
		for (const inc of includeDirs) inputs.push(`include ${inc} ${tree.sha(inc)}`);
		for (const lists of ancestorLists) inputs.push(`cmake ${lists} ${tree.sha(lists)}`);
		for (const d of dataDirs) inputs.push(`data ${d} ${tree.sha(d)}`);
		const key = sha256(inputs.join("\n")).slice(0, kKeyLength);
		return { name: test.name, key, inputs };
	});
}

// --- markers -------------------------------------------------------------------------------------

const markerFile = (name, key) => `${encodeURIComponent(name).replaceAll("*", "%2A")}@${key}.json`;

function readMarkers(dir) {
	const markers = new Map();
	if (!dir || !fs.existsSync(dir)) return markers;
	for (const f of fs.readdirSync(dir)) {
		if (!f.endsWith(".json") || f === kSetFile) continue;
		try {
			const m = JSON.parse(fs.readFileSync(path.join(dir, f), "utf8"));
			if (m.test && m.key && f === markerFile(m.test, m.key)) markers.set(f, m);
		} catch {
			// An unreadable marker is no marker.
		}
	}
	return markers;
}

// A branch's own marker set shadows main's in the Actions cache, so a pass is trusted only this
// long: past it the test reruns, and a failure the nightly run found on main reaches the branch.
function expired(marker, now) {
	const recorded = Date.parse(marker.recorded ?? "");
	return !(now - recorded <= kMaxMarkerAgeHours * 3600 * 1000);
}

// What an input line is about: a path for the tree and blob lines, the setting for the rest.
function inputId(line) {
	const words = line.split(" ");
	return kPathLines.has(words[0]) ? words.at(-2) : words.slice(0, 2).join(" ");
}

function changedInputs(previous, current) {
	const before = new Set(previous ?? []);
	const after = new Set(current);
	const changed = new Set();
	for (const line of after) if (!before.has(line)) changed.add(inputId(line));
	for (const line of before) if (!after.has(line)) changed.add(inputId(line));
	return [...changed];
}

function runUrl(runId) {
	const server = process.env.GITHUB_SERVER_URL;
	const repo = process.env.GITHUB_REPOSITORY;
	return server && repo && runId ? `${server}/${repo}/actions/runs/${runId}` : null;
}

function appendSummary(markdown) {
	const file = process.env.GITHUB_STEP_SUMMARY;
	if (file) fs.appendFileSync(file, `${markdown}\n`);
}

function setOutputs(outputs) {
	const file = process.env.GITHUB_OUTPUT;
	for (const [k, v] of Object.entries(outputs)) {
		if (file) fs.appendFileSync(file, `${k}=${v}\n`);
	}
}

const regexEscape = (s) => s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");

// --- commands ------------------------------------------------------------------------------------

function plan(opts) {
	const buildDir = path.resolve(opts["build-dir"] ?? fail("plan needs --build-dir"));
	const rev = opts.rev ?? "HEAD";
	const graph = loadGraph(buildDir, opts.config);
	const tests = listTests(buildDir, opts.exclude, opts.config);
	const allTests = listTests(buildDir, null, opts.config);
	const keyed = computeKeys(graph, tests, allTests, rev);
	const commit = run("git", ["rev-parse", rev], { cwd: graph.sourceRoot }).trim();

	if (opts.json) {
		console.log(JSON.stringify(keyed.map(({ name, key, inputs }) => ({ name, key, inputs })), null, 1));
		return;
	}
	if (opts.explain) {
		for (const { name, key, inputs } of keyed) {
			console.log(`${name} ${key}`);
			for (const line of inputs) console.log(`  ${line}`);
		}
		if (!opts.markers) return;
	}

	const now = Date.now();
	const restored = readMarkers(opts.markers);
	const previousByTest = new Map([...restored.values()].map((m) => [m.test, m]));
	const decisions = keyed.map(({ name, key, inputs }) => {
		const marker = opts["ignore-markers"] ? null : restored.get(markerFile(name, key));
		if (marker && !expired(marker, now)) return { name, key, inputs, decision: "skip", marker };
		let why;
		if (opts["ignore-markers"]) why = "full run: markers ignored";
		else if (marker) why = `the recorded pass expired (recorded ${marker.recorded}; kept ${kMaxMarkerAgeHours} h)`;
		else if (!previousByTest.has(name)) why = "no recorded pass";
		else {
			const changed = changedInputs(previousByTest.get(name).inputs, inputs);
			why = changed.length ? `changed since the last pass: ${changed.join(", ")}` : "inputs differ from the last pass";
		}
		return { name, key, inputs, decision: "run", why };
	});

	const out = path.join(buildDir, "test-select");
	fs.mkdirSync(out, { recursive: true });
	fs.writeFileSync(
		path.join(out, "plan.json"),
		JSON.stringify({ rev, commit, sourceRoot: graph.sourceRoot, ignoreMarkers: !!opts["ignore-markers"], tests: decisions }, null, 1),
	);

	const toRun = decisions.filter((d) => d.decision === "run");
	const toSkip = decisions.filter((d) => d.decision === "skip");
	for (const d of toRun) console.log(`run   ${d.name} ${d.key}: ${d.why}`);
	for (const d of toSkip) {
		const url = runUrl(d.marker.run);
		console.log(`skip  ${d.name} ${d.key}: passed at ${d.marker.commit} on ${d.marker.recorded}${url ? ` (${url})` : ""}`);
	}

	const rows = decisions.map((d) => {
		if (d.decision === "run") return `| ${d.name} | run | \`${d.key}\` | ${d.why} |`;
		const url = runUrl(d.marker.run);
		const where = url ? `[run ${d.marker.run}](${url})` : "an earlier run";
		return `| ${d.name} | skipped | \`${d.key}\` | passed with this key at \`${d.marker.commit.slice(0, 12)}\` in ${where}, ${d.marker.recorded} |`;
	});
	appendSummary(
		[
			`### Test selection (${process.env.RUNNER_OS ?? process.platform})`,
			"",
			`${toRun.length} to run, ${toSkip.length} skipped${opts["ignore-markers"] ? " (full run: markers ignored)" : ""}. A key is the hash of everything a test reads; the test-results artifact's \`test-select/plan.json\` lists each key's inputs.`,
			"",
			"| Test | Decision | Key | Why |",
			"|---|---|---|---|",
			...rows,
			"",
		].join("\n"),
	);

	setOutputs({
		"run-count": String(toRun.length),
		"run-regex": toRun.length ? `^(${toRun.map((d) => regexEscape(d.name)).join("|")})$` : "",
	});
}

function parseJunit(file) {
	const results = new Map();
	if (!fs.existsSync(file)) return results;
	const xml = fs.readFileSync(file, "utf8");
	const decode = (s) =>
		s.replace(/&(lt|gt|quot|apos|amp);/g, (_, e) => ({ lt: "<", gt: ">", quot: '"', apos: "'", amp: "&" })[e]);
	const re = /<testcase\b([^>]*?)(\/>|>([\s\S]*?)<\/testcase>)/g;
	for (const m of xml.matchAll(re)) {
		const attrs = Object.fromEntries([...m[1].matchAll(/(\w+)="([^"]*)"/g)].map((a) => [a[1], decode(a[2])]));
		const body = m[3] ?? "";
		const passed = attrs.status === "run" && !/<(failure|error|skipped)\b/.test(body);
		results.set(attrs.name, passed ? "passed" : attrs.status || "unknown");
	}
	return results;
}

function record(opts) {
	const buildDir = path.resolve(opts["build-dir"] ?? fail("record needs --build-dir"));
	const markersDir = path.resolve(opts.markers ?? fail("record needs --markers"));
	const planFile = path.join(buildDir, "test-select", "plan.json");
	if (!fs.existsSync(planFile)) fail(`no plan at ${planFile}`);
	const planned = JSON.parse(fs.readFileSync(planFile, "utf8"));
	// The exes ran from the checkout, so a plan keyed on another tree would stamp their passes with
	// that tree's keys.
	const head = run("git", ["rev-parse", "HEAD"], { cwd: planned.sourceRoot }).trim();
	if (planned.commit !== head) fail(`the plan keyed ${planned.rev} (${planned.commit}), not HEAD (${head}); record only a plan of HEAD`);
	const results = opts.junit ? parseJunit(path.resolve(opts.junit)) : new Map();

	const keep = new Map();
	const rows = [];
	const missing = [];
	for (const d of planned.tests) {
		const file = markerFile(d.name, d.key);
		if (d.decision === "skip") {
			keep.set(file, d.marker);
			rows.push(`| ${d.name} | skipped | marker kept |`);
			continue;
		}
		const result = results.get(d.name);
		if (!result) missing.push(d.name);
		if (result === "passed") {
			keep.set(file, {
				test: d.name,
				key: d.key,
				commit: planned.commit,
				run: process.env.GITHUB_RUN_ID ?? null,
				ref: process.env.GITHUB_REF ?? null,
				recorded: new Date().toISOString(),
				inputs: d.inputs,
			});
			rows.push(`| ${d.name} | passed | marker written |`);
		} else {
			rows.push(`| ${d.name} | ${result ?? "missing from ctest's results"} | no marker |`);
		}
		console.log(`${d.name}: ${result ?? "missing from ctest's results"}`);
	}

	fs.rmSync(markersDir, { recursive: true, force: true });
	fs.mkdirSync(markersDir, { recursive: true });
	for (const [file, marker] of keep) fs.writeFileSync(path.join(markersDir, file), JSON.stringify(marker, null, 1));

	// recorded is in the digest so a set re-proven by the nightly run is saved with fresh dates; a run
	// that only carried markers forward reproduces the digest it restored.
	const entries = [...keep].map(([file, m]) => `${file} ${m.recorded}`).sort();
	const digest = sha256(entries.join("\n")).slice(0, kKeyLength);
	// Written even for an empty set, so a run where nothing passed still has a file to cache and
	// replaces the set it restored; otherwise the next run would skip on the stale passes.
	fs.writeFileSync(path.join(markersDir, kSetFile), JSON.stringify({ digest, markers: entries }, null, 1));
	console.log(`${keep.size} markers, set ${digest}`);
	appendSummary(["| Test | Result | Marker |", "|---|---|---|", ...rows, "", `Marker set \`${digest}\`.`, ""].join("\n"));
	setOutputs({ digest });
	if (missing.length) {
		fail(`planned to run but missing from ctest's results: ${missing.join(", ")}`);
	}
}

const { command, opts } = parseArgs(process.argv.slice(2));
if (command === "query") writeQueries(path.resolve(opts["build-dir"] ?? fail("query needs --build-dir")));
else if (command === "plan") plan(opts);
else if (command === "record") record(opts);
else fail("usage: test-select.mjs query|plan|record --build-dir DIR [options]; see the header of this file");
