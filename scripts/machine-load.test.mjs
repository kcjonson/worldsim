// node --test scripts/machine-load.test.mjs
import assert from 'node:assert/strict';
import test from 'node:test';
import { gpuLoadPct, kIdleClockMaxRatio, loadVerdict, parseGpuQuery } from './machine-load.mjs';

const quiet = { cpuPct: 3, games: [], builders: [] };

test('at the idle clock utilization is scaled', () => {
	// WOR-790: 39% at 450 of 2130 MHz, a card doing nothing
	const gpuPct = gpuLoadPct({ utilizationPct: 39, clockMhz: 450, maxClockMhz: 2130 });
	assert.equal(gpuPct, 8);
	assert.equal(loadVerdict({ ...quiet, gpuPct }), 'idle');
});

test('the band edge is scaled', () => {
	assert.equal(gpuLoadPct({ utilizationPct: 40, clockMhz: 2000 * kIdleClockMaxRatio, maxClockMhz: 2000 }), 10);
});

test('just above the band reads raw', () => {
	assert.equal(gpuLoadPct({ utilizationPct: 40, clockMhz: 540, maxClockMhz: 2130 }), 40);
});

test('a mid clock reads raw and stays busy', () => {
	const gpuPct = gpuLoadPct({ utilizationPct: 40, clockMhz: 1000, maxClockMhz: 2130 });
	assert.equal(gpuPct, 40);
	assert.equal(loadVerdict({ ...quiet, gpuPct }), 'busy');
});

test('full clock reads raw', () => {
	assert.equal(gpuLoadPct({ utilizationPct: 50, clockMhz: 1980, maxClockMhz: 2130 }), 50);
});

test('a missing clock or maximum reads raw', () => {
	assert.equal(gpuLoadPct({ utilizationPct: 39, clockMhz: Number.NaN, maxClockMhz: 2130 }), 39);
	assert.equal(gpuLoadPct({ utilizationPct: 39, clockMhz: 450, maxClockMhz: Number.NaN }), 39);
});

test('a zero clock or maximum reads raw', () => {
	assert.equal(gpuLoadPct({ utilizationPct: 39, clockMhz: 0, maxClockMhz: 2130 }), 39);
	assert.equal(gpuLoadPct({ utilizationPct: 39, clockMhz: 450, maxClockMhz: 0 }), 39);
});

test('a query line parses to its three fields and the load', () => {
	assert.deepEqual(parseGpuQuery('39, 450, 2130\n'), { utilizationPct: 39, clockMhz: 450, maxClockMhz: 2130, pct: 8 });
});

test('an unavailable field', () => {
	assert.equal(parseGpuQuery('[N/A], 450, 2130'), null);
	assert.equal(parseGpuQuery('39, [N/A], 2130').pct, 39);
	assert.equal(parseGpuQuery('39, 450, [N/A]').pct, 39);
});

test('an empty line is no reading, not 0%', () => {
	assert.equal(parseGpuQuery(''), null);
	assert.equal(parseGpuQuery('\n'), null);
});

test('only the first GPU is read', () => {
	assert.equal(parseGpuQuery('39, 450, 2130\n90, 2000, 2130\n').utilizationPct, 39);
});

test('no GPU reading is judged on CPU alone', () => {
	assert.equal(loadVerdict({ ...quiet, gpuPct: null }), 'idle');
	assert.equal(loadVerdict({ ...quiet, cpuPct: 40, gpuPct: null }), 'busy');
});

test('a game is busy and a builder is mostly idle', () => {
	assert.equal(loadVerdict({ ...quiet, gpuPct: 0, games: ['world-sim'] }), 'busy');
	assert.equal(loadVerdict({ ...quiet, gpuPct: 0, builders: ['ninja'] }), 'mostly-idle');
});

test('the 10 and 35 boundaries', () => {
	assert.equal(loadVerdict({ ...quiet, gpuPct: 9 }), 'idle');
	assert.equal(loadVerdict({ ...quiet, gpuPct: 10 }), 'mostly-idle');
	assert.equal(loadVerdict({ ...quiet, gpuPct: 34 }), 'mostly-idle');
	assert.equal(loadVerdict({ ...quiet, gpuPct: 35 }), 'busy');
});
