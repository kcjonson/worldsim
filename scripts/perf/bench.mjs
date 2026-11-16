// One run of a Google Benchmark executable (a library's *-benchmarks target), with the rest of the
// machine watched the way a capture watches it. These time chunk and planet generation directly,
// which the frame capture sees only as scroll spikes. CPU-only: no game, no GPU hold.

import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { startLoadSampler, startProcessWatch, voidReasons } from './capture.mjs';

const kToMs = { ns: 1e-6, us: 1e-3, ms: 1, s: 1e3 };
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// Answers { executable, when, void, voidReasons, benchmarks: [{ name, realTimeMs, cpuTimeMs, iterations }], samples }.
// realTimeMs is the one compared: the planet pipeline benchmark works on another thread.
export async function runBenchmarks({ exe, log = console.log }) {
	const report = path.join(os.tmpdir(), `bench-${process.pid}-${Date.now()}.json`);
	const child = spawn(exe, [`--benchmark_out=${report}`, '--benchmark_out_format=json'], { cwd: path.dirname(exe), stdio: 'ignore' });
	const exited = new Promise((resolve, reject) => {
		child.on('error', reject);
		child.on('exit', resolve);
	});
	// After the spawn: typeperf resolves its process counters once, when it starts
	const image = path.basename(exe).replace(/\.exe$/i, '');
	const sampler = startLoadSampler(child.pid, image);
	const processWatch = startProcessWatch(image);
	const samples = [];
	let running = true;
	exited.then(() => { running = false; }, () => { running = false; });
	try {
		while (running) {
			await sleep(2000);
			samples.push({ pstate: null, ...sampler.read() });
		}
		const code = await exited;
		if (code !== 0) throw new Error(`${image} exited with ${code}`);
	} finally {
		sampler.stop();
		processWatch.stop();
		if (running) child.kill();
	}

	const json = JSON.parse(fs.readFileSync(report, 'utf8'));
	fs.rmSync(report, { force: true });
	const benchmarks = json.benchmarks
		.filter((entry) => entry.run_type !== 'aggregate' && !entry.error_occurred)
		.map((entry) => ({
			name: entry.name,
			realTimeMs: entry.real_time * kToMs[entry.time_unit],
			cpuTimeMs: entry.cpu_time * kToMs[entry.time_unit],
			iterations: entry.iterations,
		}));
	for (const entry of benchmarks) log(`${entry.name.padEnd(48)} ${entry.realTimeMs.toPrecision(4)} ms`);

	const reasons = voidReasons([{ samples, ...processWatch.take() }], { gpu: false });
	return { executable: image, when: new Date().toISOString(), void: reasons.length > 0, voidReasons: reasons, benchmarks, samples };
}
