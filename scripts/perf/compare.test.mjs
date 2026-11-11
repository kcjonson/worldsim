// node --test scripts/perf/compare.test.mjs
import assert from 'node:assert/strict';
import test from 'node:test';
import { compareRuns } from './compare.mjs';

function run(gpuMs, { isVoid = false, spikes33 = 0 } = {}) {
	const sample = { frameTimeMs: 8.33, p99Ms: 8.4, gpuMs, tileGpuMs: 0.5, spikes33, drawCalls: 60, triangles: 1000 };
	const scenario = (name) => ({ scenario: name, samples: Array.from({ length: 9 }, () => ({ ...sample })) });
	return { void: isVoid, voidReasons: [], scenarios: [scenario('idle zoom=3'), scenario('scroll zoom=3')] };
}

test('two runs a side within noise are clean', () => {
	assert.equal(compareRuns([run(2.00), run(2.02)], [run(2.01), run(2.03)]).verdict, 'clean');
});

test('past noise but under the major threshold is regressed', () => {
	const result = compareRuns([run(2.00), run(2.01)], [run(2.10), run(2.11)]);
	assert.equal(result.verdict, 'regressed');
});

test('past 10% and 0.25 ms on gpuMs is major', () => {
	assert.equal(compareRuns([run(2.00), run(2.01)], [run(2.60), run(2.61)]).verdict, 'major');
});

test('faster is clean', () => {
	assert.equal(compareRuns([run(2.60), run(2.61)], [run(2.00), run(2.01)]).verdict, 'clean');
});

test('one run a side flags only a major-sized step', () => {
	assert.equal(compareRuns([run(2.00)], [run(2.10)]).verdict, 'clean');
	assert.equal(compareRuns([run(2.00)], [run(2.60)]).verdict, 'major');
});

test('a loaded tip cannot confirm a regression, but still shows a clean result', () => {
	const worse = compareRuns([run(2.00), run(2.01)], [run(2.60, { isVoid: true }), run(2.61)]);
	assert.equal(worse.verdict, 'inconclusive');
	assert.equal(worse.found, 'major');
	assert.equal(compareRuns([run(2.00), run(2.01)], [run(2.00, { isVoid: true }), run(2.01)]).verdict, 'clean');
});

test('a loaded base cannot confirm a clean result, but a regression against it stands', () => {
	assert.equal(compareRuns([run(2.00, { isVoid: true }), run(2.01)], [run(2.00), run(2.01)]).verdict, 'inconclusive');
	assert.equal(compareRuns([run(2.00, { isVoid: true }), run(2.01)], [run(2.60), run(2.61)]).verdict, 'major');
});

test('33 ms spikes in every tip scroll run and no base run is major', () => {
	const result = compareRuns([run(2.00), run(2.01)], [run(2.00, { spikes33: 3 }), run(2.01, { spikes33: 2 })]);
	assert.equal(result.verdict, 'major');
	assert.deepEqual(result.scenarios.filter((scenario) => scenario.newSpikes).map((scenario) => scenario.scenario), ['scroll zoom=3']);
});
