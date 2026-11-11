// Is the machine quiet enough to time anything? docs/testing/README.md, "Performance".
// Usage: node scripts/machine-load.mjs [--seconds 5]
// Prints one JSON line: { verdict: "idle" | "mostly-idle" | "busy", cpuPct, gpuPct, processes }.
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

function gpuPct() {
	try {
		const out = execFileSync('nvidia-smi', ['--query-gpu=utilization.gpu', '--format=csv,noheader,nounits'], { encoding: 'utf8' });
		return Number(out.split('\n')[0].trim());
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
		gpuSamples.push(gpuPct());
		await new Promise((resolve) => setTimeout(resolve, 1000));
	}
	const after = cpuTimes();

	const cpu = Math.round(100 * (1 - (after.idle - before.idle) / (after.total - before.total)));
	const gpuReadings = gpuSamples.filter((value) => value !== null && Number.isFinite(value));
	const gpu = gpuReadings.length > 0 ? Math.max(...gpuReadings) : null;

	const running = new Set(processNames());
	const games = kGames.filter((name) => running.has(name));
	const builders = [...running].filter(isBuilder);

	const worst = Math.max(cpu, gpu ?? 0);
	let verdict = 'busy';
	if (games.length === 0 && worst < kMostlyIdleMaxPct) {
		verdict = builders.length === 0 && worst < kIdleMaxPct ? 'idle' : 'mostly-idle';
	}
	return { verdict, cpuPct: cpu, gpuPct: gpu, processes: [...games, ...builders] };
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
