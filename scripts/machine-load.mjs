// Is the machine quiet enough to time anything? docs/testing/README.md, "Performance".
// Usage: node scripts/machine-load.mjs [--seconds 5]
// Prints one JSON line: { verdict: "idle" | "mostly-idle" | "busy", cpuPct, gpuPct, gpuUtilizationPct,
// gpuClockMhz, gpuMaxClockMhz, processes }. gpuPct, the figure the verdict is judged on, is
// utilization, scaled by graphics clock over its maximum only while the card sits at its idle clock
// (gpuLoadPct); the other gpu fields are the busiest sample's raw readings.
// Run it before a timed perf run and again after; the game being timed must not be running.

import { execFile, execFileSync } from 'node:child_process';
import os from 'node:os';
import { pathToFileURL } from 'node:url';

export const kIdleMaxPct = 10;
export const kMostlyIdleMaxPct = 35;
export const kGames = ['world-sim', 'ui-sandbox', 'asset-manager'];
const kBuilders = ['cl', 'link', 'ninja', 'cmake', 'ctest', 'ccache', 'clang', 'clang++', 'clang-cl', 'worldgen-cli'];

function cpuTimes() {
	let idle = 0;
	let total = 0;
	for (const cpu of os.cpus()) {
		for (const value of Object.values(cpu.times)) total += value;
		idle += cpu.times.idle;
	}
	return { idle, total };
}

export const kIdleClockMaxRatio = 0.25;

// A card at its lowest clock reports 25 to 45% utilization, which is a few percent of its real
// capacity, so in that band utilization is scaled by clock over maximum clock. Anywhere else, or
// without both clocks, the raw utilization stands.
export function gpuLoadPct({ utilizationPct, clockMhz, maxClockMhz }) {
	const clockRatio = clockMhz / maxClockMhz;
	const atIdleClock = clockMhz > 0 && maxClockMhz > 0 && clockRatio <= kIdleClockMaxRatio;
	return Math.round(atIdleClock ? utilizationPct * clockRatio : utilizationPct);
}

function cellNumber(cell) {
	const text = (cell ?? '').trim();
	return text === '' ? Number.NaN : Number(text);
}

// One line of `--query-gpu=utilization.gpu,clocks.current.graphics,clocks.max.graphics` output.
// Only the first GPU is read. No utilization means no reading, never 0%.
export function parseGpuQuery(out) {
	const [utilizationPct, clockMhz, maxClockMhz] = out.split('\n')[0].split(',').map(cellNumber);
	if (!Number.isFinite(utilizationPct)) return null;
	return { utilizationPct, clockMhz, maxClockMhz, pct: gpuLoadPct({ utilizationPct, clockMhz, maxClockMhz }) };
}

export function loadVerdict({ cpuPct, gpuPct, games, builders }) {
	const worst = Math.max(cpuPct, gpuPct ?? 0);
	if (games.length > 0 || worst >= kMostlyIdleMaxPct) return 'busy';
	return builders.length === 0 && worst < kIdleMaxPct ? 'idle' : 'mostly-idle';
}

// One query, so utilization and clock belong to the same instant
function gpuSample() {
	try {
		const query = '--query-gpu=utilization.gpu,clocks.current.graphics,clocks.max.graphics';
		return parseGpuQuery(execFileSync('nvidia-smi', [query, '--format=csv,noheader,nounits'], { encoding: 'utf8' }));
	} catch {
		return null;
	}
}

const kListProcesses = process.platform === 'win32' ? ['tasklist', ['/FO', 'CSV', '/NH']] : ['ps', ['-A', '-o', 'comm=']];

function parseProcessNames(out) {
	return out.split('\n')
		.map((line) => line.split(',')[0].replaceAll('"', '').trim())
		.map((name) => name.split('/').pop().replace(/\.exe$/i, '').toLowerCase())
		.filter(Boolean);
}

// Lower-case image names, one entry per running process
export function processNames() {
	return parseProcessNames(execFileSync(...kListProcesses, { encoding: 'utf8' }));
}

// The same without blocking, for a caller that is timing something meanwhile
export function processNamesAsync() {
	return new Promise((resolve, reject) => {
		execFile(...kListProcesses, { encoding: 'utf8' }, (error, out) => (error ? reject(error) : resolve(parseProcessNames(out))));
	});
}

export function isBuilder(name) {
	return kBuilders.includes(name) || name.endsWith('-tests');
}

export async function readMachineLoad({ seconds = 5 } = {}) {
	const before = cpuTimes();
	const gpuSamples = [];
	for (let i = 0; i < seconds; i++) {
		const sample = gpuSample();
		if (sample !== null) gpuSamples.push(sample);
		await new Promise((resolve) => setTimeout(resolve, 1000));
	}
	const after = cpuTimes();

	const cpu = Math.round(100 * (1 - (after.idle - before.idle) / (after.total - before.total)));
	// The busiest sample by the scaled figure, with the raw figures that produced it
	const gpu = gpuSamples.reduce((busiest, sample) => (busiest === null || sample.pct > busiest.pct ? sample : busiest), null);

	const running = new Set(processNames());
	const games = kGames.filter((name) => running.has(name));
	const builders = [...running].filter(isBuilder);

	const gpuPct = gpu?.pct ?? null;
	return {
		verdict: loadVerdict({ cpuPct: cpu, gpuPct, games, builders }),
		cpuPct: cpu,
		gpuPct,
		gpuUtilizationPct: gpu?.utilizationPct ?? null,
		gpuClockMhz: Number.isFinite(gpu?.clockMhz) ? gpu.clockMhz : null,
		gpuMaxClockMhz: Number.isFinite(gpu?.maxClockMhz) ? gpu.maxClockMhz : null,
		processes: [...games, ...builders],
	};
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
	const secondsArg = process.argv.indexOf('--seconds');
	const seconds = secondsArg > 0 ? Number(process.argv[secondsArg + 1]) : 5;
	if (!Number.isFinite(seconds) || seconds <= 0) {
		console.error('--seconds takes a positive number');
		process.exit(2);
	}
	console.log(JSON.stringify(await readMachineLoad({ seconds })));
}
