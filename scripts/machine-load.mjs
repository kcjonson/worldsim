// Is the machine quiet enough to time anything? docs/testing/README.md, "Performance".
// Usage: node scripts/machine-load.mjs [--seconds 5]
// Prints one JSON line: { verdict: "idle" | "mostly-idle" | "busy", cpuPct, gpuPct, processes }.
// Run it before a timed perf run and again after; the game being timed must not be running.

import { execFileSync } from 'node:child_process';
import os from 'node:os';

const kIdleMaxPct = 10;
const kMostlyIdleMaxPct = 35;
const kGames = ['world-sim', 'ui-sandbox', 'asset-manager'];
const kBuilders = ['cl', 'link', 'ninja', 'cmake', 'ctest', 'ccache', 'clang', 'clang++', 'clang-cl', 'worldgen-cli'];

const secondsArg = process.argv.indexOf('--seconds');
const seconds = secondsArg > 0 ? Number(process.argv[secondsArg + 1]) : 5;
if (!Number.isFinite(seconds) || seconds <= 0) {
	console.error('--seconds takes a positive number');
	process.exit(2);
}

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

function processNames() {
	const out = process.platform === 'win32'
		? execFileSync('tasklist', ['/FO', 'CSV', '/NH'], { encoding: 'utf8' })
		: execFileSync('ps', ['-A', '-o', 'comm='], { encoding: 'utf8' });
	return out.split('\n')
		.map((line) => line.split(',')[0].replaceAll('"', '').trim())
		.map((name) => name.split('/').pop().replace(/\.exe$/i, '').toLowerCase())
		.filter(Boolean);
}

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
const builders = [...running].filter((name) => kBuilders.includes(name) || name.endsWith('-tests'));

const worst = Math.max(cpu, gpu ?? 0);
let verdict = 'busy';
if (games.length === 0 && worst < kMostlyIdleMaxPct) {
	verdict = builders.length === 0 && worst < kIdleMaxPct ? 'idle' : 'mostly-idle';
}

console.log(JSON.stringify({ verdict, cpuPct: cpu, gpuPct: gpu, processes: [...games, ...builders] }));
