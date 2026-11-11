// Perf tooling: the capture a session runs on its own change, and the steps of the nightly batch.
// docs/testing/README.md ("Performance") is the rule; docs/testing/nightly-perf-batch.md the batch.
//
//   capture   --port 8081 (--view river|coast|desert | --x 0 --y 20) [--zooms 3,1.5] [--no-scroll]
//             [--no-gpu-hold] [--out file]      against a world-sim you started, in the game scene
//   compare   --base a.json[,b.json] --tip c.json[,d.json] [--md file]
//             --night <dir> --base <sha> --tip <sha> [--views river,coast] [--runs 2]
//             (a night's views and runs are what measure was given; fewer captures is inconclusive)
//   idle-wait [--until 06:00] [--interval-seconds 300] [--max-minutes n]
//   range                                       the perf-relevant merges since the last tested SHA
//   measure   --refs <base>,<tip> [--views ...] [--runs 2] [--stop-at 07:00] [--night <dir>]
//   state     [--set-tested <sha>] [--open-regression '<json>'] [--close-regression <pr>]
//   setup                                       the batch's worktree and its first state
//
// Progress goes to stderr; stdout is one JSON document. idle-wait and measure run for a long time.

import { execFileSync, spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { readMachineLoad } from './machine-load.mjs';
import { capture, kDefaultZooms } from './perf/capture.mjs';
import { compareRuns, normalizeCapture, toMarkdown, worstVerdict } from './perf/compare.mjs';

const kViews = { river: [0, 20], coast: [70030, 0], desert: [540, 131480] };
const kPerfPaths = [/^libs\/(world-view|world|sim|assets)\//, /^apps\/world-sim\//, /^assets\//];
const kNeverPerf = [/\.test\.cpp$/, /\.md$/];
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
	return { sha, pr, title, url, perfLine, salalBump, perfFiles: perfFiles.slice(0, 20), relevant: why.length > 0, why: why.join('; ') };
}

function range() {
	requireBuildTree();
	const state = readState();
	if (!state.lastTested) fail('no lastTested in state; run: node scripts/perf.mjs setup');
	git(buildTree, 'fetch', '--quiet', 'origin', 'main');
	const tip = git(buildTree, 'rev-parse', 'origin/main');
	const shas = git(buildTree, 'rev-list', '--first-parent', '--reverse', `${state.lastTested}..${tip}`).split('\n').filter(Boolean);
	const merges = shas.map(mergeInfo);
	return { base: state.lastTested, tip, merges, relevant: merges.filter((merge) => merge.relevant).map((merge) => merge.sha), openRegressions: state.openRegressions };
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

async function snapshot(sha, night, deadline) {
	const dir = path.join(nightlyDir, 'builds', sha);
	if (fs.existsSync(path.join(dir, '.complete'))) return dir;

	const logPath = path.join(night, `build-${sha.slice(0, 12)}.log`);
	const log = fs.openSync(logPath, 'a');
	const run = (...args) => runUntil(deadline, 'cmake', args, { cwd: buildTree, log });
	progress(`building ${sha.slice(0, 12)} (log: ${logPath})`);
	try {
		git(buildTree, 'checkout', '--quiet', '--detach', sha);
		if (!fs.existsSync(path.join(buildTree, 'build', 'CMakeCache.txt'))) await run('--preset', kWindows ? 'windows' : 'default');
		if (kWindows) await run('--build', 'build', '--config', 'RelWithDebInfo', '--target', 'world-sim');
		else await run('--build', 'build', '--target', 'world-sim');
	} finally {
		fs.closeSync(log);
	}

	const exeDir = kWindows ? path.join(buildTree, 'build', 'apps', 'world-sim', 'RelWithDebInfo') : path.join(buildTree, 'build', 'apps', 'world-sim');
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

async function measure(options) {
	requireBuildTree();
	if (!fs.existsSync(kSharedPlanet)) fail(`no planet at ${kSharedPlanet}; bake it from the main checkout (CLAUDE.md, "Sharing the pre-generated world")`);
	if (typeof options.refs !== 'string') fail('measure needs --refs <ref>[,<ref>...]');
	const views = (typeof options.views === 'string' ? options.views : 'river,coast,desert').split(',');
	for (const view of views) if (!(view in kViews)) fail(`unknown view: ${view}`);
	const runs = options.runs ? Number(options.runs) : 2;
	const stopAt = nextTime(typeof options['stop-at'] === 'string' ? options['stop-at'] : '07:00');
	const gate = !options['no-idle-gate'];
	const night = nightDir(options);
	const result = { night, views, runs, refs: {}, captures: [], buildFailed: [], stopped: null };

	git(buildTree, 'fetch', '--quiet', 'origin');
	const snapshots = {};
	try {
		for (const ref of options.refs.split(',')) {
			const sha = git(buildTree, 'rev-parse', `${ref}^{commit}`);
			result.refs[ref] = sha;
			if (Date.now() > stopAt.getTime()) { result.stopped = 'deadline'; break; }
			try {
				snapshots[sha] = await snapshot(sha, night, stopAt);
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
		// Back to main's tip, so the next command runs the current tooling
		git(buildTree, 'checkout', '--quiet', '--detach', 'origin/main');
	}
	if (result.stopped) return result;

	const waitForIdle = async () => (await idleWait({ until: stopAt, intervalSeconds: 300 })).idle;

	// Interleaved: every SHA's first run before any SHA's second
	outer: for (let run = 1; run <= runs; run++) {
		for (const [sha, dir] of Object.entries(snapshots)) {
			captures: for (const view of views) {
				const file = path.join(night, 'captures', `${sha.slice(0, 12)}-${view}-${run}.json`);
				if (fs.existsSync(file) && !options.force) {
					result.captures.push({ sha, view, run, file, reused: true, void: JSON.parse(fs.readFileSync(file, 'utf8')).void });
					continue;
				}
				let taken = null;
				for (let attempt = 1; attempt <= 2; attempt++) {
					if (Date.now() > stopAt.getTime()) { result.stopped = 'deadline'; break outer; }
					if (gate && (await readMachineLoad({ seconds: 5 })).verdict !== 'idle' && !(await waitForIdle())) { result.stopped = 'never idle'; break outer; }
					progress(`capture ${sha.slice(0, 12)} ${view} run ${run}${attempt > 1 ? ' (redo)' : ''}`);
					const [x, y] = kViews[view];
					try {
						taken = await withGame(dir, kBatchPort, (gamePid) => capture({ port: kBatchPort, x, y, gamePid, log: progress }));
					} catch (error) {
						// A build that won't launch or answer is a result for the report, not a reason to lose the night
						progress(`capture failed: ${error.message}`);
						result.captures.push({ sha, view, run, file: null, error: error.message });
						continue captures;
					}
					if (!taken.void) break;
					progress(`void: ${taken.voidReasons.join('; ')}`);
				}
				fs.writeFileSync(file, `${JSON.stringify({ sha, view, run, ...taken }, null, 1)}\n`);
				result.captures.push({ sha, view, run, file, void: taken.void, voidReasons: taken.voidReasons });
			}
		}
	}
	return result;
}

// --- compare ---

function loadCaptures(files) {
	return files.map((file) => normalizeCapture(JSON.parse(fs.readFileSync(file, 'utf8'))));
}

function compare(options) {
	if (typeof options.base !== 'string' || typeof options.tip !== 'string') fail('compare needs --base and --tip');
	const sets = {};
	// What the night was asked to cover: every view, this many runs a side. Zero outside a night,
	// where the files named are the whole comparison.
	let expectedRuns = 0;
	let mdPath = typeof options.md === 'string' ? path.resolve(options.md) : null;
	if (options.night) {
		const night = path.resolve(options.night);
		const base = git(buildTree, 'rev-parse', `${options.base}^{commit}`);
		const tip = git(buildTree, 'rev-parse', `${options.tip}^{commit}`);
		expectedRuns = options.runs ? Number(options.runs) : 2;
		const all = fs.readdirSync(path.join(night, 'captures'));
		const filesOf = (sha, view) => all.filter((name) => name.startsWith(`${sha.slice(0, 12)}-${view}-`)).map((name) => path.join(night, 'captures', name));
		for (const view of (typeof options.views === 'string' ? options.views : Object.keys(kViews).join(',')).split(',')) {
			if (!(view in kViews)) fail(`unknown view: ${view}`);
			sets[view] = [filesOf(base, view), filesOf(tip, view)];
		}
		mdPath ??= path.join(night, `compare-${base.slice(0, 12)}-${tip.slice(0, 12)}.md`);
	} else {
		sets.capture = [options.base.split(','), options.tip.split(',')];
	}
	if (Object.values(sets).every(([baseFiles, tipFiles]) => baseFiles.length === 0 && tipFiles.length === 0)) fail('no captures to compare');

	const views = {};
	const markdown = [];
	for (const [view, [baseFiles, tipFiles]] of Object.entries(sets)) {
		const incomplete = baseFiles.length < expectedRuns || tipFiles.length < expectedRuns
			? `captures incomplete: base ${baseFiles.length} and tip ${tipFiles.length} of ${expectedRuns} runs`
			: null;
		if (baseFiles.length === 0 || tipFiles.length === 0) {
			markdown.push(`### ${view}: inconclusive (${incomplete})`);
			views[view] = { verdict: 'inconclusive', found: null, incomplete };
			continue;
		}
		let result;
		try {
			result = compareRuns(loadCaptures(baseFiles), loadCaptures(tipFiles));
		} catch (error) {
			fail(`${view}: ${error.message}`);
		}
		// Coverage the night asked for and didn't get can't pass as clean, or as only a minor
		// regression. A major step in what was captured stands.
		if (incomplete && result.verdict !== 'major') {
			result.verdict = 'inconclusive';
			result.incomplete = incomplete;
		}
		markdown.push(toMarkdown(view, result));
		views[view] = {
			verdict: result.verdict, found: result.found, baseVoid: result.baseVoid, tipVoid: result.tipVoid, incomplete,
			newSpikes: result.scenarios.filter((scenario) => scenario.newSpikes).map((scenario) => scenario.scenario),
			moved: result.scenarios.flatMap((scenario) => scenario.cells
				.filter((cell) => cell.regressed || cell.improved)
				.map((cell) => ({ scenario: scenario.scenario, metric: cell.metric, base: cell.base, tip: cell.tip, noise: cell.noise, major: cell.major, improved: cell.improved }))),
			counts: Object.fromEntries(result.scenarios.map((scenario) => [scenario.scenario, scenario.counts])),
		};
	}
	if (mdPath) fs.writeFileSync(mdPath, `${markdown.join('\n\n')}\n`);
	else console.error(markdown.join('\n\n'));
	return { verdict: worstVerdict(Object.values(views).map((view) => view.verdict)), markdown: mdPath, views };
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

function setup() {
	git(mainRoot, 'fetch', '--quiet', 'origin', 'main');
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
	case 'range': emit(range()); break;
	case 'measure': emit(await measure(options)); break;
	case 'state': emit(stateCommand(options)); break;
	case 'setup': emit(setup()); break;
	default: fail('usage: node scripts/perf.mjs capture|compare|idle-wait|range|measure|state|setup (see the header of this file)');
}
