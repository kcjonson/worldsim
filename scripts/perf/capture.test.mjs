// node --test scripts/perf/capture.test.mjs
import assert from 'node:assert/strict';
import test from 'node:test';
import { parseLoadLine } from './capture.mjs';

const context = { gamePid: 200, cpuCount: 10 };
const total = '"\\\\HOST\\Processor(_Total)\\% Processor Time"';
const game = '"\\\\HOST\\Process V2(world-sim:200)\\% Processor Time"';
const gpu = (pid) => `"\\\\HOST\\GPU Engine(pid_${pid}_luid_0x0_0x1_phys_0_eng_0_engtype_3D)\\Utilization Percentage"`;
const line = (cells) => cells.map((cell) => (cell.startsWith('"') ? cell : `"${cell}"`)).join(',');

test('the game is told from other load by PID', () => {
	const { header } = parseLoadLine(line(['"(PDH-CSV 4.0)"', total, game, gpu(100), gpu(200)]), null, context);
	const { row } = parseLoadLine(line(['11/16/2026 02:29:00.000', '12', '90', '5', '80']), header, context);
	assert.deepEqual(row, { foreignCpuPct: 3, foreignGpuPct: 5 });
});

test('real load on another process still counts', () => {
	const { header } = parseLoadLine(line(['"(PDH-CSV 4.0)"', total, game, gpu(100), gpu(200)]), null, context);
	const { row } = parseLoadLine(line(['11/16/2026 02:29:00.000', '40', '90', '70', '80']), header, context);
	assert.deepEqual(row, { foreignCpuPct: 31, foreignGpuPct: 70 });
});

// typeperf left Processor(_Total) out of its header but not its rows, so the game's GPU use read
// as another process's and the total CPU as nothing (the 2026-11-16 river voids)
test('a header that names fewer columns than its rows carry is never read against', () => {
	assert.deepEqual(parseLoadLine(line(['"(PDH-CSV 4.0)"', game, gpu(100), gpu(200)]), null, context), { misaligned: true });
	const { header } = parseLoadLine(line(['"(PDH-CSV 4.0)"', total, game, gpu(100)]), null, context);
	assert.deepEqual(parseLoadLine(line(['11/16/2026 02:29:00.000', '12', '90', '5', '80']), header, context), { misaligned: true });
});
