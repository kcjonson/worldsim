// Perf tooling: the capture a session runs on its own change, and the steps of the nightly batch.
// docs/testing/README.md ("Performance") is the rule; docs/testing/nightly-perf-batch.md the batch.
//
//   capture   --port 8081 (--view river|coast|desert | --x 0 --y 20) [--zooms 3,1.5] [--no-scroll]
//             [--no-gpu-hold] [--no-reveal] [--out file]      against a world-sim you started, in the game
//             scene, with player visibility revealed unless --no-reveal (which times the cover)
//   compare   --base a.json[,b.json] --tip c.json[,d.json] [--md file]
//             --night <dir> --base <sha> --tip <sha> [--views river,coast|none] [--bench world,assets]
//             [--runs 2]   (a night's views, targets and runs are what measure and bench were given;
//             fewer captures or runs than that is inconclusive)
//   idle-wait [--until 06:00] [--interval-seconds 300] [--max-minutes n]
//   range                                       the perf-relevant merges since the last tested SHA
//   measure   --refs <base>,<tip> [--views ...] [--runs 2] [--stop-at 07:00] [--night <dir>]
//   bench     --refs <base>,<tip> --targets world,assets,worldgen [--runs 2] [--stop-at 07:00] [--night <dir>]
//             --exe <a *-benchmarks executable> [--out file]      one run of one you built
//   state     [--set-tested <sha>] [--open-regression '<json>'] [--close-regression <pr>]
//   setup                                       the batch's worktree and its first state
//
// Progress goes to stderr; stdout is one JSON document. idle-wait, measure and bench run for a long time.

import { execFileSync, spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { readMachineLoad } from './machine-load.mjs';
import { capture, kDefaultZooms } from './perf/capture.mjs';
import { runBenchmarks } from './perf/bench.mjs';
import { benchToMarkdown, compareBenchRuns, compareRuns, normalizeCapture, toMarkdown, worstVerdict } from './perf/compare.mjs';

const kViews = { river: [0, 20], coast: [70030, 0], desert: [540, 131480] };
const kPerfPaths = [/^libs\/(world-view|world|sim|assets)\//, /^apps\/world-sim\//, /^assets\//];
const kNeverPerf = [/\.test\.cpp$/, /\.bench\.cpp$/, /\.md$/];
// Each library's Google Benchmark executable (<name>-benchmarks) and the source it times. `world`
// samples chunks through worldgen's data, so a worldgen change reaches it too.
const kBenchTargets = {
	world: [/^libs\/(world|worldgen)\//],
	assets: [/^libs\/assets\//],
	worldgen: [/^libs\/worldgen\//],
};
const kIdleChecksNeeded = 2;
const kBatchPort = 8097;
const kWindows = process.platform === 'win32';

const here = path.dirname(fileURLToPath(import.meta.url));
const git = (cwd, ...args) => execFileSync('git', args, { cwd, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] }).trim();
const mainRoot = path.dirname(git(here, 'rev-parse', '--path-format=absolute', '--git-common-dir'));
const nightlyDir = path.join(mainRoot, 'perf-results', 'nightly');
const buildTree = path.join(mainRoot, '.claude', 'worktrees', 'perf-nightly');
const statePath = path.join(nightlyDir, 'state.json');
const kSharedPlanet = path.join(mainRoot, 'planets', 'quickstart.wsplanet');

const progress = (text) => console.error(`[${new Date().toTimeString().slice(0, 8)}] ${text}`);
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const emit = (value) => console.log(JSON.stringify(value, null, 2));

// GitHub's SSH endpoint times out now and then, and one bad minute shouldn't cost the night
async function fetchOrigin(cwd, ...refs) {
	for (let attempt = 1; ; attempt++) {
		try {
			git(cwd, 'fetch', '--quiet', 'origin', ...refs);
			return;
		} catch (error) {
			if (attempt === 3) throw error;
			progress(`git fetch failed, trying again (${attempt} of 3)`);
			await sleep(20000);
		}
	}
}

function fail(message) {
	console.error(message);
	process.exit(2);
}

function parseArgs(argv) {
	const options = {};
	for (let i = 0; i < argv.length; i++) {
		if (!argv[i].startsWith('--')) fail(`unexpected argument: ${argv[i]}`);
		const next = argv[i + 1];
		if (next === undefined || next.startsWith('--')) {
			options[argv[i].slice(2)] = true;
		} else {
			options[argv[i].slice(2)] = next;
			i++;
		}
	}
	return options;
}

// The next time the local clock reads HH:MM
function nextTime(text) {
	const match = /^(\d{1,2}):(\d{2})$/.exec(text);
	if (!match) fail(`not a time: ${text} (want HH:MM)`);
	const when = new Date();
	when.setHours(Number(match[1]), Number(match[2]), 0, 0);
	if (when <= new Date()) when.setDate(when.getDate() + 1);
	return when;
}

function readState() {
	return fs.existsSync(statePath) ? JSON.parse(fs.readFileSync(statePath, 'utf8')) : { lastTested: null, openRegressions: [] };
}

function writeState(state) {
	fs.mkdirSync(nightlyDir, { recursive: true });
	fs.writeFileSync(statePath, `${JSON.stringify(state, null, 2)}\n`);
}

function nightDir(options) {
	const now = new Date();
	const date = `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, '0')}-${String(now.getDate()).padStart(2, '0')}`;
	const dir = options.night ? path.resolve(options.night) : path.join(nightlyDir, date);
	fs.mkdirSync(path.join(dir, 'captures'), { recursive: true });
	return dir;
}

function requireBuildTree() {
	if (!fs.existsSync(buildTree)) fail(`no batch worktree at ${buildTree}; run: node scripts/perf.mjs setup`);
}

// --- idle-wait ---

async function idleWait({ until, intervalSeconds = 300 }) {
	const checks = [];
	let streak = 0;
	for (;;) {
		const load = await readMachineLoad({ seconds: 5 });
		checks.push({ at: new Date().toISOString(), ...load });
		streak = load.verdict === 'idle' ? streak + 1 : 0;
		progress(`machine ${load.verdict}: cpu ${load.cpuPct}% gpu ${load.gpuPct ?? '?'}% ${load.processes.join(',')} (${streak} of ${kIdleChecksNeeded})`);
		if (streak >= kIdleChecksNeeded) return { idle: true, checks };
		if (Date.now() + intervalSeconds * 1000 > until.getTime()) return { idle: false, checks };
		await sleep(intervalSeconds * 1000);
	}
}

function waitOptions(options, untilDefault) {
	const until = options['max-minutes']
		? new Date(Date.now() + Number(options['max-minutes']) * 60000)
		: nextTime(typeof options.until === 'string' ? options.until : untilDefault);
	return { until, intervalSeconds: options['interval-seconds'] ? Number(options['interval-seconds']) : 300 };
}

// --- range ---

function mergeInfo(sha) {
	const subject = git(buildTree, 'log', '-1', '--format=%s', sha);
	const prMatch = /Merge pull request #(\d+)/.exec(subject) ?? /\(#(\d+)\)$/.exec(subject);
	const pr = prMatch ? Number(prMatch[1]) : null;
	const files = git(buildTree, 'diff', '--name-only', `${sha}^1`, sha).split('\n').filter(Boolean);
	const perfFiles = files.filter((file) => kPerfPaths.some((pattern) => pattern.test(file)) && !kNeverPerf.some((pattern) => pattern.test(file)));
	const salalBump = /^[+-]\s*GIT_TAG/m.test(git(buildTree, 'diff', `${sha}^1`, sha, '--', 'CMakeLists.txt'));
	const timed = files.filter((file) => !kNeverPerf.some((pattern) => pattern.test(file)));
	const bench = Object.keys(kBenchTargets).filter((target) => salalBump || timed.some((file) => kBenchTargets[target].some((pattern) => pattern.test(file))));
	// A benchmark that itself changed measures something else on each side
	const benchRewritten = Object.keys(kBenchTargets).filter((target) => files.some((file) => file.startsWith(`libs/${target}/`) && file.endsWith('.bench.cpp')));

	let title = subject;
	let url = null;
	let perfLine = null;
	if (pr !== null) {
		try {
			const view = JSON.parse(execFileSync('gh', ['pr', 'view', String(pr), '--json', 'title,url,body'], { cwd: buildTree, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] }));
			title = view.title;
			url = view.url;
			perfLine = /^\s*Perf:\s*(.+)$/mi.exec(view.body ?? '')?.[1].trim() ?? null;
		} catch {
			// gh missing or offline: the paths still decide
		}
	}

	const saysPerf = perfLine !== null && !/^none\b/i.test(perfLine);
	const why = [];
	if (perfFiles.length > 0) why.push(`touches ${perfFiles.length} perf-relevant file${perfFiles.length === 1 ? '' : 's'}`);
	if (salalBump) why.push('changes the salal GIT_TAG');
	if (saysPerf) why.push('its Perf: line names a cost');
	return { sha, pr, title, url, perfLine, salalBump, perfFiles: perfFiles.slice(0, 20), relevant: why.length > 0, why: why.join('; '), bench, benchRewritten };
}

async function range() {
	requireBuildTree();
	const state = readState();
	if (!state.lastTested) fail('no lastTested in state; run: node scripts/perf.mjs setup');
	await fetchOrigin(buildTree, 'main');
	const tip = git(buildTree, 'rev-parse', 'origin/main');
	const shas = git(buildTree, 'rev-list', '--first-parent', '--reverse', `${state.lastTested}..${tip}`).split('\n').filter(Boolean);
	const merges = shas.map(mergeInfo);
	const benchRewritten = [...new Set(merges.flatMap((merge) => merge.benchRewritten))];
	return {
		base: state.lastTested, tip, merges,
		relevant: merges.filter((merge) => merge.relevant).map((merge) => merge.sha),
		benchTargets: [...new Set(merges.flatMap((merge) => merge.bench))].filter((target) => !benchRewritten.includes(target)),
		benchRewritten,
		openRegressions: state.openRegressions,
	};
}

// --- measure ---

class DeadlineError extends Error {}

// Runs a command to its end or to the deadline, whichever is first. At the deadline the whole
// process tree goes: killing cmake alone leaves ninja and the compilers holding the machine.
function runUntil(deadline, command, args, { cwd, log }) {
	return new Promise((resolve, reject) => {
		const child = spawn(command, args, { cwd, stdio: ['ignore', log, log], detached: !kWindows });
		const timer = setTimeout(() => {
			try {
				if (kWindows) execFileSync('taskkill', ['/PID', String(child.pid), '/T', '/F'], { stdio: 'ignore' });
				else process.kill(-child.pid);
			} catch {
				// already gone
			}
			reject(new DeadlineError(`${command} was still running at the deadline`));
		}, Math.max(0, deadline.getTime() - Date.now()));
		child.on('error', (error) => {
			clearTimeout(timer);
			reject(error);
		});
		child.on('exit', (code) => {
			clearTimeout(timer);
			if (code === 0) resolve();
			else reject(new Error(`${command} exited with ${code}`));
		});
	});
}

// Checks the batch worktree out at a SHA and builds targets there, RelWithDebInfo
async function buildAt(sha, night, deadline, targets) {
	const logPath = path.join(night, `build-${sha.slice(0, 12)}.log`);
	const log = fs.openSync(logPath, 'a');
	const run = (...args) => runUntil(deadline, 'cmake', args, { cwd: buildTree, log });
	progress(`building ${targets.join(', ')} at ${sha.slice(0, 12)} (log: ${logPath})`);
	try {
		git(buildTree, 'checkout', '--quiet', '--detach', sha);
		if (!fs.existsSync(path.join(buildTree, 'build', 'CMakeCache.txt'))) await run('--preset', kWindows ? 'windows' : 'default');
		if (kWindows) await run('--build', 'build', '--config', 'RelWithDebInfo', '--target', ...targets);
		else await run('--build', 'build', '--target', ...targets);
	} finally {
		fs.closeSync(log);
	}
}

// Where a target's exe lands under the batch worktree's build
const builtDir = (...parts) => (kWindows ? path.join(buildTree, 'build', ...parts, 'RelWithDebInfo') : path.join(buildTree, 'build', ...parts));

// The benchmark executables of one SHA, each copied with its DLLs to bench-builds/<sha>/<target>/.
// Answers { target: exe path }.
async function benchSnapshot(sha, night, deadline, targets) {
	const dirOf = (target) => path.join(nightlyDir, 'bench-builds', sha, target);
	const missing = targets.filter((target) => !fs.existsSync(path.join(dirOf(target), '.complete')));
	if (missing.length > 0) await buildAt(sha, night, deadline, missing.map((target) => `${target}-benchmarks`));
	for (const target of missing) {
		const exeName = `${target}-benchmarks${kWindows ? '.exe' : ''}`;
		const from = builtDir('libs', target);
		fs.rmSync(dirOf(target), { recursive: true, force: true });
		fs.mkdirSync(dirOf(target), { recursive: true });
		for (const name of fs.readdirSync(from)) {
			if (name === exeName || /\.(dll|so|dylib)$/i.test(name)) fs.copyFileSync(path.join(from, name), path.join(dirOf(target), name));
		}
		fs.writeFileSync(path.join(dirOf(target), '.complete'), `${new Date().toISOString()}\n`);
	}
	return Object.fromEntries(targets.map((target) => [target, path.join(dirOf(target), `${target}-benchmarks${kWindows ? '.exe' : ''}`)]));
}

async function snapshot(sha, night, deadline) {
	const dir = path.join(nightlyDir, 'builds', sha);
	if (fs.existsSync(path.join(dir, '.complete'))) return dir;
	await buildAt(sha, night, deadline, ['world-sim']);

	const exeDir = builtDir('apps', 'world-sim');
	fs.rmSync(dir, { recursive: true, force: true });
	fs.cpSync(exeDir, dir, { recursive: true, filter: (source) => !/\.(pdb|ilk|wsplanet)$/i.test(source) });

	// The planet the game is played on, never a fresh bake: a planet baked from a later worldgen is a
	// different world, and its timings compare with nothing. A hard link, so no copy per SHA.
	const staged = path.join(dir, 'assets', 'planets', 'quickstart.wsplanet');
	fs.mkdirSync(path.dirname(staged), { recursive: true });
	try {
		fs.linkSync(kSharedPlanet, staged);
	} catch {
		fs.copyFileSync(kSharedPlanet, staged);
	}
	fs.writeFileSync(path.join(dir, '.complete'), `${new Date().toISOString()}\n`);
	return dir;
}

async function answers(url, timeoutMs = 2000) {
	try {
		return (await fetch(url, { signal: AbortSignal.timeout(timeoutMs) })).ok;
	} catch {
		return false;
	}
}

async function withGame(dir, port, body) {
	const base = `http://127.0.0.1:${port}`;
	if (await answers(`${base}/api/state?what=scene`)) throw new Error(`something already answers on port ${port}`);
	const child = spawn(path.join(dir, kWindows ? 'world-sim.exe' : 'world-sim'), ['--scene=game', '--http-port', String(port)], { cwd: dir, stdio: 'ignore' });
	let exited = false;
	child.on('exit', () => { exited = true; });
	child.on('error', () => { exited = true; });
	try {
		const deadline = Date.now() + 180000;
		while (!(await answers(`${base}/api/state?what=colonists`))) {
			if (exited) throw new Error('the game exited before its scene was ready');
			if (Date.now() > deadline) throw new Error('the game scene was not ready after 180 s');
			await sleep(500);
		}
		return await body(child.pid);
	} finally {
		if (!exited) await answers(`${base}/api/control?action=exit`, 60000);
		for (let i = 0; i < 20 && !exited; i++) await sleep(500);
		if (!exited) child.kill();
	}
}

// The options measure and bench share, and the start of their result
function nightRun(options, what) {
	requireBuildTree();
	if (typeof options.refs !== 'string') fail(`${what} needs --refs <ref>[,<ref>...]`);
	return {
		refs: options.refs.split(','),
		runs: options.runs ? Number(options.runs) : 2,
		stopAt: nextTime(typeof options['stop-at'] === 'string' ? options['stop-at'] : '07:00'),
		gate: !options['no-idle-gate'],
		force: Boolean(options.force),
		night: nightDir(options),
	};
}

// Builds every ref with `build(sha)`, which answers what a take needs, then puts the batch
// worktree back on main's tip. Stops at the deadline.
async function buildRefs({ refs, stopAt }, result, build) {
	// The refs are usually here already (range fetched them), so a fetch that fails costs nothing
	// unless one won't resolve, and that fails by itself below
	await fetchOrigin(buildTree).catch(() => progress('git fetch failed; going on with the refs this checkout has'));
	const built = {};
	try {
		for (const ref of refs) {
			const sha = git(buildTree, 'rev-parse', `${ref}^{commit}`);
			result.refs[ref] = sha;
			if (Date.now() > stopAt.getTime()) { result.stopped = 'deadline'; break; }
			try {
				built[sha] = await build(sha);
			} catch (error) {
				if (error instanceof DeadlineError) {
					progress(`build of ${sha.slice(0, 12)} stopped at the deadline`);
					result.stopped = 'deadline';
					break;
				}
				progress(`build of ${sha.slice(0, 12)} failed: ${error.message.split('\n')[0]}`);
				result.buildFailed.push(sha);
			}
		}
	} finally {
		// So the next command runs the current tooling
		git(buildTree, 'checkout', '--quiet', '--detach', 'origin/main');
	}
	return built;
}

// One take of every item (a view, a benchmark target) at every SHA, `runs` times, interleaved:
// every SHA's first run before any SHA's second. The machine is checked before each take, and a
// void one is redone once after it is idle again.
async function takeInterleaved({ runs, stopAt, gate, force, night }, result, { built, items, key, folder, take }) {
	const waitForIdle = async () => (await idleWait({ until: stopAt, intervalSeconds: 300 })).idle;
	fs.mkdirSync(path.join(night, folder), { recursive: true });
	for (let run = 1; run <= runs; run++) {
		for (const [sha, from] of Object.entries(built)) {
			items: for (const item of items) {
				const file = path.join(night, folder, `${sha.slice(0, 12)}-${item}-${run}.json`);
				if (fs.existsSync(file) && !force) {
					result.taken.push({ sha, [key]: item, run, file, reused: true, void: JSON.parse(fs.readFileSync(file, 'utf8')).void });
					continue;
				}
				let taken = null;
				for (let attempt = 1; attempt <= 2; attempt++) {
					if (Date.now() > stopAt.getTime()) { result.stopped = 'deadline'; return; }
					if (gate && (await readMachineLoad({ seconds: 5 })).verdict !== 'idle' && !(await waitForIdle())) { result.stopped = 'never idle'; return; }
					progress(`${sha.slice(0, 12)} ${item} run ${run}${attempt > 1 ? ' (redo)' : ''}`);
					try {
						taken = await take(from, item);
					} catch (error) {
						// A build that won't launch or answer is a result for the report, not a reason to lose the night
						progress(`failed: ${error.message}`);
						result.taken.push({ sha, [key]: item, run, file: null, error: error.message });
						continue items;
					}
					if (!taken.void) break;
					progress(`void: ${taken.voidReasons.join('; ')}`);
				}
				fs.writeFileSync(file, `${JSON.stringify({ sha, [key]: item, run, ...taken }, null, 1)}\n`);
				result.taken.push({ sha, [key]: item, run, file, void: taken.void, voidReasons: taken.voidReasons });
			}
		}
	}
}

async function measure(options) {
	const run = nightRun(options, 'measure');
	if (!fs.existsSync(kSharedPlanet)) fail(`no planet at ${kSharedPlanet}; bake it from the main checkout (CLAUDE.md, "Sharing the pre-generated world")`);
	const views = (typeof options.views === 'string' ? options.views : 'river,coast,desert').split(',');
	for (const view of views) if (!(view in kViews)) fail(`unknown view: ${view}`);
	const result = { night: run.night, views, runs: run.runs, refs: {}, taken: [], buildFailed: [], stopped: null };

	const built = await buildRefs(run, result, (sha) => snapshot(sha, run.night, run.stopAt));
	if (result.stopped) return result;
	await takeInterleaved(run, result, {
		built, items: views, key: 'view', folder: 'captures',
		take: (dir, view) => {
			const [x, y] = kViews[view];
			return withGame(dir, kBatchPort, (gamePid) => capture({ port: kBatchPort, x, y, gamePid, log: progress }));
		},
	});
	return result;
}

// --- bench ---

function benchTargets(text) {
	const targets = text.split(',');
	for (const target of targets) if (!(target in kBenchTargets)) fail(`unknown benchmark target: ${target} (have ${Object.keys(kBenchTargets).join(', ')})`);
	return targets;
}

async function bench(options) {
	// A session's own run: one executable it built, timed where it stands
	if (typeof options.exe === 'string') {
		const result = await runBenchmarks({ exe: path.resolve(options.exe), log: progress });
		const stamp = new Date().toISOString().slice(0, 19).replaceAll(':', '').replace('T', '-');
		const out = path.resolve(typeof options.out === 'string' ? options.out : path.join('perf-results', `bench-${stamp}.json`));
		fs.mkdirSync(path.dirname(out), { recursive: true });
		fs.writeFileSync(out, `${JSON.stringify(result, null, 1)}\n`);
		if (result.void) progress(`the machine was loaded during this run (${result.voidReasons.join('; ')}): a worse result from it is unconfirmed, and it sets no baseline`);
		return { file: out, void: result.void, voidReasons: result.voidReasons };
	}

	const run = nightRun(options, 'bench');
	if (typeof options.targets !== 'string') fail('bench needs --targets <target>[,<target>...] or --exe <path>');
	const targets = benchTargets(options.targets);
	const result = { night: run.night, targets, runs: run.runs, refs: {}, taken: [], buildFailed: [], stopped: null };

	const built = await buildRefs(run, result, (sha) => benchSnapshot(sha, run.night, run.stopAt, targets));
	if (result.stopped) return result;
	await takeInterleaved(run, result, {
		built, items: targets, key: 'target', folder: 'bench',
		take: (exes, target) => runBenchmarks({ exe: exes[target], log: progress }),
	});
	return result;
}

// --- compare ---

const readJson = (file) => JSON.parse(fs.readFileSync(file, 'utf8'));

function compare(options) {
	if (typeof options.base !== 'string' || typeof options.tip !== 'string') fail('compare needs --base and --tip');
	// One entry a view or benchmark target: { kind, name, baseFiles, tipFiles }
	const sets = [];
	// What the night was asked to cover: this many runs a side of everything named. Zero outside
	// a night, where the files named are the whole comparison.
	let expectedRuns = 0;
	let mdPath = typeof options.md === 'string' ? path.resolve(options.md) : null;
	if (options.night) {
		const night = path.resolve(options.night);
		const base = git(buildTree, 'rev-parse', `${options.base}^{commit}`);
		const tip = git(buildTree, 'rev-parse', `${options.tip}^{commit}`);
		expectedRuns = options.runs ? Number(options.runs) : 2;
		const filesOf = (folder, sha, item) => {
			const dir = path.join(night, folder);
			if (!fs.existsSync(dir)) return [];
			return fs.readdirSync(dir).filter((name) => name.startsWith(`${sha.slice(0, 12)}-${item}-`)).map((name) => path.join(dir, name));
		};
		const views = typeof options.views === 'string' ? options.views : Object.keys(kViews).join(',');
		for (const view of views === 'none' ? [] : views.split(',')) {
			if (!(view in kViews)) fail(`unknown view: ${view}`);
			sets.push({ kind: 'view', name: view, baseFiles: filesOf('captures', base, view), tipFiles: filesOf('captures', tip, view) });
		}
		for (const target of typeof options.bench === 'string' ? benchTargets(options.bench) : []) {
			sets.push({ kind: 'bench', name: target, baseFiles: filesOf('bench', base, target), tipFiles: filesOf('bench', tip, target) });
		}
		mdPath ??= path.join(night, `compare-${base.slice(0, 12)}-${tip.slice(0, 12)}.md`);
	} else {
		const baseFiles = options.base.split(',');
		const first = readJson(baseFiles[0]);
		const kind = 'benchmarks' in first ? 'bench' : 'view';
		const name = kind === 'bench' ? (first.executable ?? 'run').replace(/-benchmarks$/, '') : 'capture';
		sets.push({ kind, name, baseFiles, tipFiles: options.tip.split(',') });
	}
	if (sets.every((set) => set.baseFiles.length === 0 && set.tipFiles.length === 0)) fail('nothing to compare');

	const views = {};
	const benchmarks = {};
	const markdown = [];
	for (const { kind, name, baseFiles, tipFiles } of sets) {
		const into = kind === 'bench' ? benchmarks : views;
		const incomplete = baseFiles.length < expectedRuns || tipFiles.length < expectedRuns
			? `incomplete: base ${baseFiles.length} and tip ${tipFiles.length} of ${expectedRuns} runs`
			: null;
		if (baseFiles.length === 0 || tipFiles.length === 0) {
			markdown.push(`### ${name}${kind === 'bench' ? ' benchmarks' : ''}: inconclusive (${incomplete})`);
			into[name] = { verdict: 'inconclusive', found: null, incomplete };
			continue;
		}
		let result;
		try {
			result = kind === 'bench'
				? compareBenchRuns(baseFiles.map(readJson), tipFiles.map(readJson))
				: compareRuns(baseFiles.map((file) => normalizeCapture(readJson(file))), tipFiles.map((file) => normalizeCapture(readJson(file))));
		} catch (error) {
			fail(`${name}: ${error.message}`);
		}
		// Coverage the night asked for and didn't get can't pass as clean, or as only a minor
		// regression. A major step in what was measured stands.
		if (incomplete && result.verdict !== 'major') {
			result.verdict = 'inconclusive';
			result.incomplete = incomplete;
		}
		const summary = { verdict: result.verdict, found: result.found, baseVoid: result.baseVoid, tipVoid: result.tipVoid, incomplete };
		if (kind === 'bench') {
			markdown.push(benchToMarkdown(name, result));
			into[name] = {
				...summary,
				moved: result.cells
					.filter((cell) => cell.regressed || cell.improved)
					.map((cell) => ({ benchmark: cell.benchmark, baseMs: cell.base, tipMs: cell.tip, share: cell.share, noiseMs: cell.noise, major: cell.major, improved: cell.improved })),
			};
		} else {
			markdown.push(toMarkdown(name, result));
			into[name] = {
				...summary,
				newSpikes: result.scenarios.filter((scenario) => scenario.newSpikes).map((scenario) => scenario.scenario),
				moved: result.scenarios.flatMap((scenario) => scenario.cells
					.filter((cell) => cell.regressed || cell.improved)
					.map((cell) => ({ scenario: scenario.scenario, metric: cell.metric, base: cell.base, tip: cell.tip, noise: cell.noise, major: cell.major, improved: cell.improved }))),
				counts: Object.fromEntries(result.scenarios.map((scenario) => [scenario.scenario, scenario.counts])),
			};
		}
	}
	if (mdPath) fs.writeFileSync(mdPath, `${markdown.join('\n\n')}\n`);
	else console.error(markdown.join('\n\n'));
	const verdict = worstVerdict([...Object.values(views), ...Object.values(benchmarks)].map((entry) => entry.verdict));
	return { verdict, markdown: mdPath, views, benchmarks };
}

// --- capture, for a session's own change ---

function onlyGamePid() {
	if (!kWindows) return null;
	const rows = execFileSync('tasklist', ['/FO', 'CSV', '/NH', '/FI', 'IMAGENAME eq world-sim.exe'], { encoding: 'utf8' })
		.split('\n').filter((line) => line.startsWith('"world-sim.exe"'));
	return rows.length === 1 ? Number(rows[0].split('","')[1]) : null;
}

async function captureCommand(options) {
	const port = options.port ? Number(options.port) : 8081;
	const [x, y] = typeof options.view === 'string'
		? kViews[options.view] ?? fail(`unknown view: ${options.view}`)
		: [Number(options.x ?? 256), Number(options.y ?? 256)];
	const result = await capture({
		port, x, y, gamePid: onlyGamePid(), log: progress,
		zooms: typeof options.zooms === 'string' ? options.zooms.split(',').map(Number) : kDefaultZooms,
		scroll: !options['no-scroll'],
		gpuHold: !options['no-gpu-hold'],
		reveal: !options['no-reveal'],
	});
	const stamp = new Date().toISOString().slice(0, 19).replaceAll(':', '').replace('T', '-');
	const out = path.resolve(typeof options.out === 'string' ? options.out : path.join('perf-results', `capture-${stamp}.json`));
	fs.mkdirSync(path.dirname(out), { recursive: true });
	fs.writeFileSync(out, `${JSON.stringify(result, null, 1)}\n`);
	if (result.void) progress(`the machine was loaded during this capture (${result.voidReasons.join('; ')}): a worse result from it is unconfirmed, and it sets no baseline`);
	return { file: out, void: result.void, voidReasons: result.voidReasons };
}

// --- state, setup ---

function stateCommand(options) {
	const state = readState();
	if (typeof options['set-tested'] === 'string') state.lastTested = git(buildTree, 'rev-parse', `${options['set-tested']}^{commit}`);
	if (typeof options['open-regression'] === 'string') state.openRegressions.push({ found: new Date().toISOString().slice(0, 10), ...JSON.parse(options['open-regression']) });
	if (options['close-regression']) state.openRegressions = state.openRegressions.filter((entry) => String(entry.pr) !== String(options['close-regression']));
	if (Object.keys(options).length > 0) writeState(state);
	return state;
}

async function setup() {
	await fetchOrigin(mainRoot, 'main');
	const created = !fs.existsSync(buildTree);
	if (created) git(mainRoot, 'worktree', 'add', '--detach', buildTree, 'origin/main');
	const state = readState();
	if (!state.lastTested) {
		state.lastTested = git(mainRoot, 'rev-parse', 'origin/main');
		writeState(state);
	}
	return { buildTree, createdBuildTree: created, nightlyDir, state };
}

const [command, ...rest] = process.argv.slice(2);
const options = parseArgs(rest);
switch (command) {
	case 'capture': emit(await captureCommand(options)); break;
	case 'compare': emit(compare(options)); break;
	case 'idle-wait': {
		const waited = await idleWait(waitOptions(options, '06:00'));
		emit(waited);
		process.exitCode = waited.idle ? 0 : 3;
		break;
	}
	case 'range': emit(await range()); break;
	case 'measure': emit(await measure(options)); break;
	case 'bench': emit(await bench(options)); break;
	case 'state': emit(stateCommand(options)); break;
	case 'setup': emit(await setup()); break;
	default: fail('usage: node scripts/perf.mjs capture|compare|idle-wait|range|measure|bench|state|setup (see the header of this file)');
}
