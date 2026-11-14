// node --test scripts/perf/compare.test.mjs
import assert from 'node:assert/strict';
import test from 'node:test';
import { compareBenchRuns, compareRuns, coverUnjudged, worstVerdict } from './compare.mjs';

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

test('a loaded run supplies no noise, so it cannot hide a slower tip', () => {
	const result = compareRuns([run(2.00), run(2.01)], [run(3.00), run(4.00, { isVoid: true })]);
	assert.equal(result.verdict, 'inconclusive');
	assert.equal(result.found, 'major');
});

test('captures with no scenario in common are refused', () => {
	const other = run(2.00);
	for (const scenario of other.scenarios) scenario.scenario += ' elsewhere';
	assert.throws(() => compareRuns([run(2.00)], [other]), /share no scenario/);
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

const withCover = (capture, revealVerb) => ({ ...capture, revealVerb });

test('a cover capture of a base with no cover against a tip with one is unjudged, either way round', () => {
	const noCover = [withCover(run(2.00), false), withCover(run(2.01), false)];
	const cover = [withCover(run(3.00), true), withCover(run(3.01), true)];
	assert.match(coverUnjudged(noCover, cover), /base predates/);
	assert.match(coverUnjudged(cover, noCover), /tip has no/);
});

test('cover captures with the cover on both sides are judged like any other view', () => {
	const cover = (gpuMs) => [withCover(run(gpuMs), true), withCover(run(gpuMs + 0.01), true)];
	assert.equal(coverUnjudged(cover(2.0), cover(2.6)), null);
	assert.equal(compareRuns(cover(2.0), cover(2.6)).verdict, 'major');
});

test('captures from before revealVerb was recorded count as having no cover', () => {
	assert.equal(coverUnjudged([run(2.00)], [run(2.00)]), null);
	assert.match(coverUnjudged([run(2.00)], [withCover(run(2.00), true)]), /base predates/);
});

test('an unjudged cell never raises a night above clean', () => {
	assert.equal(worstVerdict(['unjudged']), 'clean');
	assert.equal(worstVerdict(['unjudged', 'clean']), 'clean');
	assert.equal(worstVerdict(['unjudged', 'regressed']), 'regressed');
	assert.equal(worstVerdict(['unjudged', 'major']), 'major');
});

const bench = (ms, isVoid = false) => ({ void: isVoid, benchmarks: [{ name: 'BM_Build', realTimeMs: ms }, { name: 'BM_Lookup', realTimeMs: 0.000004 }] });

test('benchmarks are judged by share of base, whatever their scale', () => {
	assert.equal(compareBenchRuns([bench(10.0), bench(10.1)], [bench(10.05), bench(10.15)]).verdict, 'clean');
	assert.equal(compareBenchRuns([bench(10.0), bench(10.1)], [bench(11.0), bench(11.1)]).verdict, 'regressed');
	assert.equal(compareBenchRuns([bench(10.0), bench(10.1)], [bench(13.0), bench(13.1)]).verdict, 'major');
	assert.equal(compareBenchRuns([bench(10.0), bench(10.1)], [bench(13.0, true), bench(13.1)]).verdict, 'inconclusive');
});
