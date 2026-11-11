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
//       valid marker, and nothing else.

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
	"scripts", // nothing in CMake reads it; scripts/ci is added back below
	".github", // tests.yml and .github/actions are added back below
]);
const kIgnoredTopLevelSuffix = [".md"];
const kGithubInputs = [".github/workflows/tests.yml", ".github/actions"];

// Properties that change only when or how long ctest runs a test, never what it runs.
const kSchedulingProperties = new Set(["TIMEOUT", "PROCESSORS", "COST", "RESOURCE_LOCK", "RUN_SERIAL", "LABELS"]);

const kKeyLength = 20;

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

// Repo-relative form of a path CMake reported, or null when it is outside the repo.
function repoRelative(p, sourceRoot) {
	const abs = path.isAbsolute(p) ? p : path.join(sourceRoot, p);
	if (!isUnder(abs, sourceRoot)) return null;
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

	const targets = new Map();
	for (const t of cfg.targets) {
		const json = read(t.jsonFile);
		const dir = repoRelative(json.paths.source, sourceRoot);
		const sources = [];
		for (const s of json.sources ?? []) {
			if (s.isGenerated) continue;
			// The target's dir covers its own sources, except at the root, which would cover everything.
			const rel = repoRelative(s.path, sourceRoot);
			if (rel && (!dir || dir === "." || !isUnder(rel, dir))) sources.push(rel);
		}
		const includes = new Set();
		for (const group of json.compileGroups ?? []) {
			for (const inc of group.includes ?? []) {
				const rel = repoRelative(inc.path, sourceRoot);
				if (rel && !isUnder(inc.path, buildRoot)) includes.add(rel);
			}
		}
		targets.set(t.id, {
			id: t.id,
			name: t.name,
			type: json.type,
			dir,
			sources: [...new Set(sources)].sort(),
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
		.map((i) => repoRelative(i.path, sourceRoot))
		.filter(Boolean);

	return { sourceRoot, buildRoot, config: cfg.name, targets, toolchains, cmakeInputs };
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

function gitObjects(sourceRoot, rev, paths) {
	const unique = [...new Set(paths)];
	if (!unique.length) return new Map();
	const out = run("git", ["cat-file", "--batch-check"], {
		cwd: sourceRoot,
		input: unique.map((p) => `${rev}:${p}\n`).join(""),
	});
	const lines = out.split("\n").filter(Boolean);
	if (lines.length !== unique.length) fail("git cat-file answered a different number of lines than it was asked");
	const result = new Map();
	unique.forEach((p, i) => {
		const line = lines[i];
		result.set(p, line.endsWith(" missing") ? "missing" : line.split(" ")[0]);
	});
	return result;
}

function topLevelEntries(sourceRoot, rev) {
	return run("git", ["ls-tree", "-z", rev], { cwd: sourceRoot })
		.split("\0")
		.filter(Boolean)
		.map((entry) => entry.slice(entry.indexOf("\t") + 1));
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

function computeKeys(graph, tests, rev) {
	const { sourceRoot, targets } = graph;
	const scriptDir = repoRelative(path.dirname(fileURLToPath(import.meta.url)), sourceRoot);
	if (!scriptDir) fail("the script must live in the repo it selects tests for");

	const targetDirs = [...targets.values()].map((t) => t.dir).filter((d) => d && d !== ".");
	const byArtifact = new Map();
	for (const t of targets.values()) for (const a of t.artifacts) byArtifact.set(a, t);

	const perTest = tests.map((test) => {
		const props = Object.fromEntries((test.properties ?? []).map((p) => [p.name, p.value]));
		const dataDirs = (props.REQUIRED_FILES ?? []).map((f) => {
			const rel = repoRelative(f, sourceRoot);
			if (!rel || isUnder(f, graph.buildRoot)) fail(`${test.name}: REQUIRED_FILES entry ${f} isn't tracked source, so no key can see it`);
			return rel;
		});
		if (!test.command?.length) fail(`${test.name}: ctest reports no command; build the tests before planning`);
		const exe = sameCase(slash(path.resolve(graph.buildRoot, test.command[0])));
		const target = byArtifact.get(exe);
		if (!target && isUnder(exe, graph.buildRoot)) fail(`${test.name}: ${test.command[0]} is no target's artifact`);
		const members = target ? closure(targets, target.id) : [];
		const commandSource = target ? null : repoRelative(test.command[0], sourceRoot);
		const runProps = Object.entries(props)
			.filter(([name]) => !kSchedulingProperties.has(name) && name !== "REQUIRED_FILES")
			.sort(([a], [b]) => a.localeCompare(b));
		// An include root wider than every closure target's own dir (planet-view exports libs/) reaches
		// headers of libraries the exe never links, so it is keyed whole.
		const ownDirs = members.map((m) => m.dir).filter((d) => d && d !== ".");
		const includeDirs = [...new Set(members.flatMap((m) => m.includes))]
			.filter((inc) => !ownDirs.some((d) => isUnder(inc, d)))
			.sort();
		return { test, target, members, dataDirs, commandSource, runProps, includeDirs };
	});

	// Top-level names a target or a declared data dir accounts for; they leave the global component.
	const claimed = new Set([...targetDirs, ...perTest.flatMap((t) => t.dataDirs)].map((p) => p.split("/")[0]));

	const globalPaths = [];
	for (const name of topLevelEntries(sourceRoot, rev)) {
		if (kIgnoredTopLevel.has(name) || claimed.has(name)) continue;
		if (kIgnoredTopLevelSuffix.some((s) => name.endsWith(s))) continue;
		globalPaths.push(name);
	}
	globalPaths.push(...kGithubInputs, scriptDir);
	// A CMake file read from a claimed dir but outside every target's dir (a shared .cmake include,
	// say) belongs to no target, so it goes in every key.
	for (const input of graph.cmakeInputs) {
		const top = input.split("/")[0];
		if (claimed.has(top) && !targetDirs.some((d) => isUnder(input, d))) globalPaths.push(input);
	}

	const allPaths = [
		...globalPaths,
		...perTest.flatMap((t) => [
			...t.members.filter((m) => m.dir && m.dir !== ".").map((m) => m.dir),
			...t.members.flatMap((m) => m.sources),
			...t.dataDirs,
			...t.includeDirs,
			...(t.commandSource ? [t.commandSource] : []),
		]),
	];
	const objects = gitObjects(sourceRoot, rev, allPaths);

	const env = [
		`env os ${process.platform}-${process.arch}${process.env.ImageOS ? ` ${process.env.ImageOS}` : ""}`,
		...graph.toolchains.map((t) => `env compiler ${t}`),
		`env config ${graph.config}`,
	];
	const global = [...new Set(globalPaths)].sort().map((p) => `global ${p} ${objects.get(p)}`);

	return perTest.map(({ test, target, members, dataDirs, commandSource, runProps, includeDirs }) => {
		const inputs = [...env, ...global];
		inputs.push(`exe ${target ? target.name : normaliseValue(test.command[0], graph)}`);
		inputs.push(`args ${normaliseValue(test.command.slice(1), graph)}`);
		for (const [name, value] of runProps) inputs.push(`property ${name} ${normaliseValue(value, graph)}`);
		for (const m of [...members].sort((a, b) => a.name.localeCompare(b.name))) {
			if (!m.dir) continue; // outside the repo: salal, keyed by its tag in the root CMakeLists.txt
			if (m.dir === ".") inputs.push(`target ${m.name} . covered-by-global`);
			else inputs.push(`target ${m.name} ${m.dir} ${objects.get(m.dir)}`);
			for (const s of m.sources) inputs.push(`source ${m.name} ${s} ${objects.get(s)}`);
		}
		for (const inc of includeDirs) inputs.push(`include ${inc} ${objects.get(inc)}`);
		if (commandSource) inputs.push(`source command ${commandSource} ${objects.get(commandSource)}`);
		for (const d of [...new Set(dataDirs)].sort()) inputs.push(`data ${d} ${objects.get(d)}`);
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
		if (!f.endsWith(".json")) continue;
		try {
			const m = JSON.parse(fs.readFileSync(path.join(dir, f), "utf8"));
			if (m.test && m.key && f === markerFile(m.test, m.key)) markers.set(f, m);
		} catch {
			// An unreadable marker is no marker.
		}
	}
	return markers;
}

// What an input line is about: a path for the tree and blob lines, the setting for the rest.
function inputId(line) {
	const words = line.split(" ");
	return ["global", "target", "source", "data", "include"].includes(words[0]) ? words.at(-2) : words.slice(0, 2).join(" ");
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
	const keyed = computeKeys(graph, tests, rev);
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

	const markers = opts["ignore-markers"] ? new Map() : readMarkers(opts.markers);
	const previousByTest = new Map([...readMarkers(opts.markers).values()].map((m) => [m.test, m]));
	const decisions = keyed.map(({ name, key, inputs }) => {
		const marker = markers.get(markerFile(name, key));
		if (marker) return { name, key, inputs, decision: "skip", marker };
		let why;
		if (opts["ignore-markers"]) why = "full run: markers ignored";
		else if (!previousByTest.has(name)) why = "no recorded pass";
		else {
			const changed = changedInputs(previousByTest.get(name).inputs, inputs);
			why = changed.length ? `changed since the last pass: ${changed.join(", ")}` : "inputs differ from the last pass";
		}
		return { name, key, inputs, decision: "run", why };
	});

	const out = path.join(buildDir, "test-select");
	fs.mkdirSync(out, { recursive: true });
	fs.writeFileSync(path.join(out, "plan.json"), JSON.stringify({ rev, commit, ignoreMarkers: !!opts["ignore-markers"], tests: decisions }, null, 1));

	const toRun = decisions.filter((d) => d.decision === "run");
	const toSkip = decisions.filter((d) => d.decision === "skip");
	for (const d of toRun) console.log(`run   ${d.name} ${d.key}: ${d.why}`);
	for (const d of toSkip) {
		const url = runUrl(d.marker.run);
		console.log(`skip  ${d.name} ${d.key}: passed at ${d.marker.commit}${url ? ` (${url})` : ""}`);
	}

	const rows = decisions.map((d) => {
		if (d.decision === "run") return `| ${d.name} | run | \`${d.key}\` | ${d.why} |`;
		const url = runUrl(d.marker.run);
		const where = url ? `[run ${d.marker.run}](${url})` : "an earlier run";
		return `| ${d.name} | skipped | \`${d.key}\` | passed with this key at \`${d.marker.commit.slice(0, 12)}\` in ${where} |`;
	});
	appendSummary(
		[
			`### Test selection (${process.env.RUNNER_OS ?? process.platform})`,
			"",
			`${toRun.length} to run, ${toSkip.length} skipped${opts["ignore-markers"] ? " (full run: markers ignored)" : ""}. A key is the hash of everything a test reads: \`node scripts/ci/test-select.mjs plan --build-dir build --explain\` lists it.`,
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
	const results = opts.junit ? parseJunit(path.resolve(opts.junit)) : new Map();

	const keep = new Map();
	const rows = [];
	for (const d of planned.tests) {
		const file = markerFile(d.name, d.key);
		if (d.decision === "skip") {
			keep.set(file, d.marker);
			rows.push(`| ${d.name} | skipped | marker kept |`);
			continue;
		}
		const result = results.get(d.name) ?? "missing from ctest's results";
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
			rows.push(`| ${d.name} | ${result} | no marker |`);
		}
		console.log(`${d.name}: ${result}`);
	}

	fs.rmSync(markersDir, { recursive: true, force: true });
	fs.mkdirSync(markersDir, { recursive: true });
	for (const [file, marker] of keep) fs.writeFileSync(path.join(markersDir, file), JSON.stringify(marker, null, 1));

	const digest = sha256([...keep.keys()].sort().join("\n")).slice(0, kKeyLength);
	console.log(`${keep.size} markers, set ${digest}`);
	appendSummary(["| Test | Result | Marker |", "|---|---|---|", ...rows, "", `Marker set \`${digest}\`.`, ""].join("\n"));
	setOutputs({ digest, count: String(keep.size) });
}

const { command, opts } = parseArgs(process.argv.slice(2));
if (command === "query") writeQueries(path.resolve(opts["build-dir"] ?? fail("query needs --build-dir")));
else if (command === "plan") plan(opts);
else if (command === "record") record(opts);
else fail("usage: test-select.mjs query|plan|record --build-dir DIR [options]; see the header of this file");
