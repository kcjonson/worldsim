# Scenario 07: In-game pause menu

**Status:** ready  
**Last verified:** 2026-10-31

Escape and the top bar's Menu button open the pause menu over the game, which pauses the sim until it closes.

---

## Steps

Launch `world-sim --scene=game --background --no-settings` (RelWithDebInfo); the dev server is on 127.0.0.1:8081. `/api/state?what=time` reads `speed` and `paused`.

1. Press Escape: a dialog titled Menu, kicker PAUSED, over the dimmed world; `paused` is `true`; Resume has focus (`/api/ui/tree`).
2. Look at the rows: Resume, Settings, Main Menu and Quit Game lit, Save and Load dimmed. Click Save: nothing happens.
3. Click Settings: the Settings scene shows (`/api/state?what=scene` lists `settings` over `game`). Press Escape: the game is back with the menu still open, `paused` still `true`.
4. Press Space: the dialog closes; `paused` is `false`.
5. Press 3, then Escape twice: the menu opens, then closes; `speed` is 3, not paused.
6. Press Space to pause, then Escape twice: the menu opens, then closes; `paused` stays `true`. Space again: `paused` is `false`.
7. Click the top bar's pause button (`top_bar_pause`): `paused` is `true`, and nothing has focus (`/api/ui/tree`). Space: `paused` is `false`.
8. Click the top bar's Menu button (`top_bar_menu`): the menu opens. Click Main Menu: the main menu scene shows.
9. Quick Start back in, Escape, then click Quit Game: the window closes and the process exits.
