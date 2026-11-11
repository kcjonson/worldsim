// The perf verdict: base against tip, each a list of captures of one view taken the same night.
// docs/testing/nightly-perf-batch.md, R5. The numbers here are the thresholds that spec names.

export const kNoiseMultiple = 3;      // regressed: worse than base by more than this many times the noise
export const kMinDeltaMs = 0.02;      // and by at least this, so two identical runs don't make noise zero
export const kMajorDeltaMs = 0.25;    // major: worse by more than this
export const kMajorShare = 0.10;      // and by more than this share of base

const kTimings = ['frameTimeMs', 'p99Ms', 'gpuMs', 'tileGpuMs', 'tileMs', 'entityMs', 'updateMs', 'sceneUpdateMs', 'sceneRenderMs'];
const kMajorTimings = ['frameTimeMs', 'gpuMs', 'tileGpuMs'];
const kCounts = ['drawCalls', 'triangles', 'uprightDrawCalls', 'uprightTriangles', 'entityCount', 'chunks'];
const kWorstOfRun = ['p99Ms', 'spikes33', ...kCounts];

// Captures before the Node port were a bare array of scenarios
export function normalizeCapture(json) {
	return Array.isArray(json) ? { void: false, voidReasons: [], scenarios: json } : json;
}

function runValue(scenario, metric) {
	const values = scenario.samples.map((sample) => sample[metric]).filter((value) => Number.isFinite(value));
	if (values.length === 0) return null;
	if (kWorstOfRun.includes(metric)) return Math.max(...values);
	values.sort((a, b) => a - b);
	return values[Math.floor(values.length / 2)];
}

const mean = (values) => values.reduce((total, value) => total + value, 0) / values.length;
const spread = (values) => Math.max(...values) - Math.min(...values);

function compareCell(baseValues, tipValues, metric) {
	const base = mean(baseValues);
	const tip = mean(tipValues);
	const delta = tip - base;
	const noiseKnown = baseValues.length > 1 && tipValues.length > 1;
	const noise = noiseKnown ? Math.max(spread(baseValues), spread(tipValues)) : null;
	const share = base > 0 ? delta / base : 0;
	const pastMajor = Math.abs(delta) > kMajorDeltaMs && Math.abs(share) > kMajorShare;
	// With one run a side there is no noise to measure against, so only a step the size of a major one counts
	const moved = noiseKnown ? Math.abs(delta) > Math.max(kNoiseMultiple * noise, kMinDeltaMs) : pastMajor;
	const regressed = moved && delta > 0;
	return {
		metric, base, tip, delta, share, noise,
		regressed,
		improved: moved && delta < 0,
		major: regressed && pastMajor && kMajorTimings.includes(metric),
	};
}

// baseRuns and tipRuns: captures (normalized) of the same view. Answers the cells that moved, the
// counts for context, and a verdict: "clean", "regressed", "major" or "inconclusive".
export function compareRuns(baseRuns, tipRuns) {
	const names = baseRuns[0].scenarios.map((scenario) => scenario.scenario)
		.filter((name) => [...baseRuns, ...tipRuns].every((run) => run.scenarios.some((scenario) => scenario.scenario === name)));
	const find = (run, name) => run.scenarios.find((scenario) => scenario.scenario === name);
	const valuesOf = (runs, name, metric) => runs.map((run) => runValue(find(run, name), metric)).filter((value) => value !== null);

	const scenarios = names.map((name) => {
		const cells = [];
		for (const metric of kTimings) {
			const baseValues = valuesOf(baseRuns, name, metric);
			const tipValues = valuesOf(tipRuns, name, metric);
			if (baseValues.length > 0 && tipValues.length > 0) cells.push(compareCell(baseValues, tipValues, metric));
		}
		const counts = {};
		for (const metric of kCounts) {
			const baseValues = valuesOf(baseRuns, name, metric);
			const tipValues = valuesOf(tipRuns, name, metric);
			if (baseValues.length > 0 && tipValues.length > 0) counts[metric] = { base: Math.max(...baseValues), tip: Math.max(...tipValues) };
		}
		// New 33 ms spikes while scrolling: every tip run spiked more than every base run
		let newSpikes = false;
		if (name.startsWith('scroll')) {
			const baseSpikes = valuesOf(baseRuns, name, 'spikes33');
			const tipSpikes = valuesOf(tipRuns, name, 'spikes33');
			newSpikes = baseSpikes.length > 0 && tipSpikes.length > 0 && Math.min(...tipSpikes) > Math.max(...baseSpikes);
			counts.spikes33 = { base: Math.max(...baseSpikes, 0), tip: Math.max(...tipSpikes, 0) };
		}
		return { scenario: name, cells, counts, newSpikes };
	});

	const anyMajor = scenarios.some((scenario) => scenario.newSpikes || scenario.cells.some((cell) => cell.major));
	const anyRegressed = scenarios.some((scenario) => scenario.cells.some((cell) => cell.regressed));
	let verdict = anyMajor ? 'major' : anyRegressed ? 'regressed' : 'clean';

	// Load only ever makes a run slower. A loaded tip can't confirm a regression, and a loaded
	// base can't confirm there isn't one.
	const baseVoid = baseRuns.some((run) => run.void);
	const tipVoid = tipRuns.some((run) => run.void);
	const unconfirmed = (tipVoid && verdict !== 'clean') || (baseVoid && verdict === 'clean');
	const found = verdict;
	if (unconfirmed) verdict = 'inconclusive';

	return { verdict, found, baseVoid, tipVoid, baseRuns: baseRuns.length, tipRuns: tipRuns.length, scenarios };
}

const kVerdictRank = ['clean', 'regressed', 'inconclusive', 'major'];
export const worstVerdict = (verdicts) => verdicts.reduce((worst, verdict) => (kVerdictRank.indexOf(verdict) > kVerdictRank.indexOf(worst) ? verdict : worst), 'clean');

// One table per view: every scenario's major timings, flagged where they moved
export function toMarkdown(view, result) {
	const fixed = (value) => (value === null ? 'n/a' : value.toFixed(2));
	const lines = [
		`### ${view}: ${result.verdict}${result.verdict === 'inconclusive' ? ` (found ${result.found}; ${result.tipVoid ? 'tip' : 'base'} capture was loaded)` : ''}`,
		'',
		`${result.baseRuns} base and ${result.tipRuns} tip runs. Times in ms as base / tip (noise); \`!\` regressed, \`!!\` major, \`+\` improved.`,
		'',
		'| Scenario | frame p50 | gpuMs | tileGpuMs | draws | triangles | upright draws |',
		'|---|---|---|---|---|---|---|',
	];
	for (const scenario of result.scenarios) {
		const timing = (metric) => {
			const cell = scenario.cells.find((candidate) => candidate.metric === metric);
			if (!cell) return 'n/a';
			const flag = cell.major ? ' `!!`' : cell.regressed ? ' `!`' : cell.improved ? ' `+`' : '';
			return `${fixed(cell.base)} / ${fixed(cell.tip)} (${fixed(cell.noise)})${flag}`;
		};
		const count = (metric) => (scenario.counts[metric] ? `${scenario.counts[metric].base} / ${scenario.counts[metric].tip}` : 'n/a');
		lines.push(`| ${scenario.scenario}${scenario.newSpikes ? ' `!! new 33 ms spikes`' : ''} | ${timing('frameTimeMs')} | ${timing('gpuMs')} | ${timing('tileGpuMs')} | ${count('drawCalls')} | ${count('triangles')} | ${count('uprightDrawCalls')} |`);
	}
	const others = result.scenarios.flatMap((scenario) => scenario.cells
		.filter((cell) => (cell.regressed || cell.improved) && !['frameTimeMs', 'gpuMs', 'tileGpuMs'].includes(cell.metric))
		.map((cell) => `- ${scenario.scenario}, ${cell.metric}: ${fixed(cell.base)} to ${fixed(cell.tip)} (noise ${fixed(cell.noise)})`));
	if (others.length > 0) lines.push('', 'Other timings that moved:', ...others);
	return lines.join('\n');
}
