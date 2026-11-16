// node --test scripts/machine-load.test.mjs
import assert from 'node:assert/strict';
import test from 'node:test';
import { loadVerdict, scaledGpuPct } from './machine-load.mjs';

const quiet = { cpuPct: 3, games: [], builders: [] };

test('an idle card at its lowest clock reads idle', () => {
	// WOR-790: 39% raw at 450 MHz on a card whose maximum is 2130 MHz
	const gpuPct = scaledGpuPct({ utilizationPct: 39, clockMhz: 450, maxClockMhz: 2130 });
	assert.ok(gpuPct < 10, `scaled ${gpuPct}`);
	assert.equal(loadVerdict({ ...quiet, gpuPct }), 'idle');
});

test('a loaded card at full clock still reads busy', () => {
	const gpuPct = scaledGpuPct({ utilizationPct: 60, clockMhz: 2100, maxClockMhz: 2130 });
	assert.ok(gpuPct > 55, `scaled ${gpuPct}`);
	assert.equal(loadVerdict({ ...quiet, gpuPct }), 'busy');
});

test('a clock above the reported maximum does not inflate the reading', () => {
	assert.equal(scaledGpuPct({ utilizationPct: 50, clockMhz: 2200, maxClockMhz: 2130 }), 50);
});

test('without clock fields the reading stays raw', () => {
	assert.equal(scaledGpuPct({ utilizationPct: 39, clockMhz: Number.NaN, maxClockMhz: Number.NaN }), 39);
});
