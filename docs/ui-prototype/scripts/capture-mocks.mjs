/*
 * Capture reference mocks of every screen and in-game sub-state into
 * docs/design/ui/mocks/. Re-runnable: tweak the look in the prototype, rerun.
 *
 * Needs the dev server running (npm run dev) and playwright-core:
 *   npm i -D playwright-core
 *   node scripts/capture-mocks.mjs
 *
 * Uses an installed browser via channel (Edge ships on Windows 11), so there
 * is no large browser download. Screens are internal React state, not routes,
 * so we click the dev-rail nav (button[data-screen]) and toggle the dev chrome
 * with Escape between shots.
 */
import { chromium } from "playwright-core";
import { mkdirSync } from "node:fs";
import { resolve, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const outDir = resolve(here, "../../design/ui/mocks");
mkdirSync(outDir, { recursive: true });

const url = process.env.PROTO_URL || "http://localhost:5174/";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const screens = [
	["splash", "splash"],
	["menu", "main-menu"],
	["scenario", "scenario-select"],
	["party", "party-selection"],
	["worldgen", "world-generation"],
	["landing", "landing-site"],
	["components", "component-gallery"],
	["game", "in-game"],
];

async function launch() {
	for (const channel of ["msedge", "chrome"]) {
		try {
			return await chromium.launch({ channel });
		} catch {
			/* try next */
		}
	}
	return await chromium.launch(); // bundled chromium, if installed
}

const browser = await launch();
const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
await page.goto(url, { waitUntil: "networkidle" });

async function railVisible() {
	return page
		.locator("button[data-screen]")
		.first()
		.isVisible()
		.catch(() => false);
}

for (const [id, name] of screens) {
	if (!(await railVisible())) await page.keyboard.press("Escape"); // show chrome to navigate
	await page.click(`button[data-screen="${id}"]`);
	await sleep(900); // let scenes/animations settle
	await page.keyboard.press("Escape"); // hide chrome for a clean shot
	await sleep(500);
	await page.screenshot({ path: resolve(outDir, `${name}.png`) });
	console.log("captured", name);

	if (id === "game") {
		try {
			await page.click('button[title="Open full dossier"]', { timeout: 3000 });
			await sleep(600);
			await page.screenshot({ path: resolve(outDir, "in-game-dossier.png") });
			console.log("captured in-game-dossier");
			await page.getByText("Gear", { exact: true }).last().click({ timeout: 3000 });
			await sleep(400);
			await page.screenshot({ path: resolve(outDir, "in-game-dossier-gear.png") });
			console.log("captured in-game-dossier-gear");
			await page.keyboard.press("Escape"); // close modal (handler stops propagation, won't toggle chrome)
			await sleep(300);
		} catch (e) {
			console.log("dossier sub-captures skipped:", e.message);
		}
	}

	await page.keyboard.press("Escape"); // restore chrome before next nav
	await sleep(200);
}

await browser.close();
console.log("done →", outDir);
