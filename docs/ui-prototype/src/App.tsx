import { useEffect, useState } from "react";
import { DevShell } from "./shell/DevShell";
import type { ScreenId, ScreenProps } from "./screens/types";

import { Splash } from "./screens/Splash/Splash";
import { MainMenu } from "./screens/MainMenu/MainMenu";
import { ScenarioSelect } from "./screens/ScenarioSelect/ScenarioSelect";
import { PartySelect } from "./screens/PartySelect/PartySelect";
import { WorldGen } from "./screens/WorldGen/WorldGen";
import { LandingSite } from "./screens/LandingSite/LandingSite";
import { InGame } from "./screens/InGame/InGame";
import { Components } from "./screens/Components/Components";

const SCREENS: Record<ScreenId, (p: ScreenProps) => JSX.Element> = {
	splash: Splash,
	menu: MainMenu,
	scenario: ScenarioSelect,
	party: PartySelect,
	worldgen: WorldGen,
	landing: LandingSite,
	game: InGame,
	components: Components,
};

export function App() {
	const [screen, setScreen] = useState<ScreenId>("splash");
	const [chromeHidden, setChromeHidden] = useState(false);

	// Esc toggles the dev chrome so screens can be reviewed clean
	useEffect(() => {
		const onKey = (e: KeyboardEvent) => {
			if (e.key === "Escape") setChromeHidden((h) => !h);
		};
		window.addEventListener("keydown", onKey);
		return () => window.removeEventListener("keydown", onKey);
	}, []);

	const Active = SCREENS[screen];

	return (
		<DevShell
			screen={screen}
			onNavigate={setScreen}
			chromeHidden={chromeHidden}
			onToggleChrome={() => setChromeHidden((h) => !h)}
		>
			<Active go={setScreen} />
		</DevShell>
	);
}
