export type ScreenId =
	| "splash"
	| "menu"
	| "scenario"
	| "party"
	| "worldgen"
	| "landing"
	| "game"
	| "components";

export interface ScreenProps {
	go: (id: ScreenId) => void;
}
