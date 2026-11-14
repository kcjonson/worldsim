// The perf capture: drives the camera through a running world-sim's dev server and records
// /api/metrics, with the game's GPU hold on so the card stays at P0 (docs/technical/ground-textures.md,
// "Clock state"). Alongside each sample it records what the rest of the machine was doing, and marks
// the capture void when that was enough to move the numbers.

import { execFileSync, spawn } from 'node:child_process';
import os from 'node:os';
import { isBuilder, kGames, processNamesAsync } from '../machine-load.mjs';

export const kDefaultZooms = [20, 8, 3, 1.5, 0.75, 0.5, 0.25];
export const kForeignMaxPct = 10;
const kForeignSampleShare = 0.2; // void when more than this share of samples saw foreign load

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

function median(values) {
	const sorted = values.filter((value) => Number.isFinite(value)).sort((a, b) => a - b);
	return sorted.length > 0 ? sorted[Math.floor(sorted.length / 2)] : null;
}

async function getJson(url, timeoutMs = 5000) {
	const response = await fetch(url, { signal: AbortSignal.timeout(timeoutMs) });
	if (!response.ok) throw new Error(`${url} answered ${response.status}`);
	return response.json();
}

// A view an older build doesn't have answers null, so a bisect can measure SHAs from before it existed
async function getJsonIfPresent(url) {
	const response = await fetch(url, { signal: AbortSignal.timeout(5000) });
	if (response.status === 404) return null;
	if (!response.ok) throw new Error(`${url} answered ${response.status}`);
	return response.json();
}

// P-state and clocks, read-only; nulls where nvidia-smi isn't installed
function gpuClocks() {
	try {
		const line = execFileSync('nvidia-smi', ['--query-gpu=pstate,clocks.gr,clocks.mem', '--format=csv,noheader'], { encoding: 'utf8' }).split('\n')[0];
		const [pstate, gr, mem] = line.split(',').map((part) => part.trim());
		return { pstate, gpuClockMHz: parseInt(gr, 10), memClockMHz: parseInt(mem, 10) };
	} catch {
		return { pstate: null, gpuClockMHz: null, memClockMHz: null };
	}
}

// CPU and GPU use by everything that isn't the game, from one typeperf process for the whole
// capture. Elsewhere than Windows, and if typeperf won't start, read() answers nulls and the
// capture is void: nobody watched the machine.
export function startLoadSampler(gamePid, gameImage) {
	const pending = [];
	let columns = null;
	let child = null;
	if (process.platform === 'win32') {
		child = spawn('typeperf', [
			'\\Processor(_Total)\\% Processor Time',
			`\\Process(${gameImage}*)\\% Processor Time`,
			'\\GPU Engine(*engtype_3D)\\Utilization Percentage',
			'-si', '1',
		], { stdio: ['ignore', 'pipe', 'ignore'] });
		child.on('error', () => { child = null; });
		let buffer = '';
		child.stdout.on('data', (chunk) => {
			buffer += chunk.toString();
			const lines = buffer.split(/\r?\n/);
			buffer = lines.pop();
			for (const line of lines) {
				if (!line.startsWith('"')) continue;
				const cells = line.slice(1, -1).split('","');
				if (cells[0].startsWith('(PDH-CSV')) {
					columns = cells.map((name) => {
						if (name.includes('\\Processor(_Total)')) return 'cpuTotal';
						if (name.includes('\\Process(')) return 'cpuGame';
						const pid = /GPU Engine\(pid_(\d+)_/.exec(name);
						if (pid) return Number(pid[1]) === gamePid ? 'gpuGame' : 'gpuForeign';
						return null;
					});
				} else if (columns) {
					const sum = { cpuTotal: 0, cpuGame: 0, gpuForeign: 0 };
					cells.forEach((cell, i) => {
						const value = Number(cell);
						if (columns[i] in sum && Number.isFinite(value)) sum[columns[i]] += value;
					});
					pending.push({
						foreignCpuPct: Math.max(0, sum.cpuTotal - sum.cpuGame / os.cpus().length),
						foreignGpuPct: sum.gpuForeign,
					});
				}
			}
		});
	}
	return {
		// The mean since the last read
		read() {
			if (pending.length === 0) return { foreignCpuPct: null, foreignGpuPct: null };
			const rows = pending.splice(0);
			const mean = (key) => Math.round(10 * rows.reduce((total, row) => total + row[key], 0) / rows.length) / 10;
			return { foreignCpuPct: mean('foreignCpuPct'), foreignGpuPct: mean('foreignGpuPct') };
		},
		stop() {
			if (child) child.kill();
		},
	};
}

// Other sessions' builds, tests and games; the one game being measured is allowed
function foreignProcesses(names, gameImage) {
	const found = new Set(names.filter(isBuilder));
	for (const game of kGames) {
		const count = names.filter((name) => name === game).length;
		if (count > (game === gameImage ? 1 : 0)) found.add(game);
	}
	return [...found];
}

// Polls the process list for the whole capture, so a build that starts and ends inside one
// scenario is still seen. take() answers what ran since the last take().
export function startProcessWatch(gameImage) {
	const seen = new Set();
	let checks = 0;
	let stopped = false;
	(async () => {
		while (!stopped) {
			const names = await processNamesAsync().catch(() => null);
			if (names) {
				checks++;
				for (const name of foreignProcesses(names, gameImage)) seen.add(name);
			}
			await sleep(1000);
		}
	})();
	return {
		take() {
			const taken = { foreignProcesses: [...seen], processChecks: checks };
			seen.clear();
			checks = 0;
			return taken;
		},
		stop() {
			stopped = true;
		},
	};
}

// `gpu: false` for a CPU-only measurement, where other processes' GPU use doesn't count
export function voidReasons(scenarios, { gpu = true } = {}) {
	const reasons = [];
	const samples = scenarios.flatMap((scenario) => scenario.samples);
	const over = (sample) => sample.foreignCpuPct > kForeignMaxPct || (gpu && sample.foreignGpuPct > kForeignMaxPct);
	const read = (sample) => sample.foreignCpuPct !== null && (!gpu || sample.foreignGpuPct !== null);
	const offP0 = samples.filter((sample) => sample.pstate !== null && sample.pstate !== 'P0').length;
	if (offP0 > 0) reasons.push(`${offP0} of ${samples.length} samples off P0`);
	const loaded = samples.filter(over).length;
	if (loaded > samples.length * kForeignSampleShare) {
		reasons.push(`${loaded} of ${samples.length} samples saw other processes over ${kForeignMaxPct}% ${gpu ? 'CPU or GPU' : 'CPU'}`);
	}
	const processes = [...new Set(scenarios.flatMap((scenario) => scenario.foreignProcesses))];
	if (processes.length > 0) reasons.push(`other sessions' processes ran: ${processes.join(', ')}`);
	// Load nobody read is unknown, not low
	const unwatched = scenarios.filter((scenario) => scenario.processChecks === 0 || !scenario.samples.some(read)).length;
	if (unwatched > 0) reasons.push(`the rest of the machine went unread in ${unwatched} of ${scenarios.length} scenarios (no typeperf readings or no process list)`);
	return reasons;
}

// Captures against a world-sim already in the game scene on `port`. `zooms` and `scroll` scope it:
// a session timing its own change passes the zooms that change touches; the nightly batch passes neither.
// `reveal` turns player visibility off for the capture (the views far from any colonist would time a
// black frame otherwise); `reveal: false` measures the cover itself. A build from before the reveal
// verb answers 404 and draws no cover, which is the same frame.
export async function capture({
	port, x, y, gamePid = null, gameImage = 'world-sim', zooms = kDefaultZooms, scroll = true,
	settleMs = 1500, gpuHold = true, gpuHoldTarget = 0.8, reveal = true, log = console.log,
}) {
	const base = `http://127.0.0.1:${port}`;
	const control = (query) => getJson(`${base}/api/control?action=${query}`);
	const camera = (query) => control(`camera&${query}`);
	const sampler = startLoadSampler(gamePid, gameImage);
	const processWatch = startProcessWatch(gameImage);
	const scenarios = [];

	async function sampleScenario(name, count = 9, intervalMs = 600) {
		const samples = [];
		for (let i = 0; i < count; i++) {
			await sleep(intervalMs);
			const metrics = await getJson(`${base}/api/metrics`);
			const uprights = await getJsonIfPresent(`${base}/api/state?what=uprights`);
			const hold = gpuHold ? await getJson(`${base}/api/state?what=gpuhold`) : null;
			samples.push({
				fps: metrics.fps, frameTimeMs: metrics.frameTimeMs, frameTimeMaxMs: metrics.frameTimeMaxMs,
				p99Ms: metrics.frameTime1PercentLow, spikes16: metrics.spikeCount16ms, spikes33: metrics.spikeCount33ms,
				drawCalls: metrics.drawCalls, triangles: metrics.triangleCount, vertices: metrics.vertexCount,
				uprightDrawCalls: uprights?.uprightDrawCalls ?? null, uprightTriangles: uprights?.uprightTriangles ?? null,
				tileMs: metrics.tileRenderMs, entityMs: metrics.entityRenderMs, updateMs: metrics.updateMs,
				sceneUpdateMs: metrics.sceneUpdateMs, sceneRenderMs: metrics.sceneRenderMs, swapMs: metrics.swapBuffersMs,
				gpuMs: metrics.gpuRenderMs, tileGpuMs: metrics.tileGpuMs,
				tileCount: metrics.tileCount, entityCount: metrics.entityCount, chunks: metrics.visibleChunkCount,
				...gpuClocks(),
				holdPasses: hold?.passes ?? null, holdGpuMs: hold?.holdGpuMs ?? null,
				...sampler.read(),
			});
		}
		const scenario = { scenario: name, samples, ...processWatch.take() };
		const pick = (key) => samples.map((sample) => sample[key]);
		const fixed = (value, digits = 2) => (value === null ? '?' : value.toFixed(digits));
		log(`${name.padEnd(18)} frame p50=${fixed(median(pick('frameTimeMs')))}ms p99max=${fixed(Math.max(...pick('p99Ms')))}ms `
			+ `gpu=${fixed(median(pick('gpuMs')))}ms tileGpu=${fixed(median(pick('tileGpuMs')))}ms `
			+ `tris=${Math.max(...pick('triangles'))} upDraws=${median(pick('uprightDrawCalls')) ?? '?'} `
			+ `otherCpu=${fixed(median(pick('foreignCpuPct')), 1)}% otherGpu=${fixed(median(pick('foreignGpuPct')), 1)}%`);
		scenarios.push(scenario);
	}

	try {
		// Vsync off so frame times show real cost (the 120 fps pacing cap still holds; gpuMs and
		// tileGpuMs show the cost under it)
		await control('vsync&value=0');
		await getJsonIfPresent(`${base}/api/dev/reveal?on=${reveal ? 1 : 0}`);
		if (gpuHold) {
			await control(`gpuhold&value=1&target=${gpuHoldTarget}`);
			await sleep(2000); // the hold settles and the driver raises the clocks
		} else {
			await sleep(500);
		}

		await camera(`x=${x}&y=${y}`);
		for (const zoom of zooms) {
			await camera(`zoom=${zoom}`);
			await sleep(settleMs); // chunk loads and bakes settle
			await sampleScenario(`idle zoom=${zoom}`);
		}

		if (scroll) {
			// Scroll right at the default zoom into fresh chunks
			await camera(`x=${x}&y=${y}&zoom=3`);
			await sleep(1000);
			await camera('panx=1');
			await sampleScenario('scroll zoom=3', 25, 400);
			await camera('panx=0&pany=0');

			// Scroll zoomed out into fresh chunks (heavy tiles)
			await camera(`x=${x}&y=${y - 2304}&zoom=0.75`);
			await sleep(2000);
			await camera('pany=-1');
			await sampleScenario('scroll zoom=0.75', 25, 400);
			await camera('panx=0&pany=0');
		}
	} finally {
		sampler.stop();
		processWatch.stop();
		if (gpuHold) await control('gpuhold&value=0').catch(() => {});
		await control('vsync&value=1').catch(() => {});
		if (reveal) await getJsonIfPresent(`${base}/api/dev/reveal?on=0`).catch(() => {});
		await camera(`x=${x}&y=${y}&zoom=3&panx=0&pany=0`).catch(() => {});
	}

	const reasons = voidReasons(scenarios);
	return { x, y, gpuHold, reveal, when: new Date().toISOString(), void: reasons.length > 0, voidReasons: reasons, scenarios };
}
