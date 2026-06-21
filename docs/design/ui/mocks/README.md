# Mocks

Rendered reference shots of every screen and in-game sub-state, captured from the prototype at 1600x1000. Each screen's spec under [../screens/](../screens/) embeds and annotates its shot; this page is the gallery.

Regenerate after any look change in the prototype: from `docs/ui-prototype/`, run `node scripts/capture-mocks.mjs` (needs the dev server up and `playwright-core` installed; it drives an installed browser via channel, no large download).

## Pre-game flow

![Splash](./splash.png)
![Main menu](./main-menu.png)
![Scenario select](./scenario-select.png)
![Party selection](./party-selection.png)
![World generation](./world-generation.png)
![Landing site](./landing-site.png)

## In-game

![In-game HUD](./in-game.png)
![Colonist dossier — Bio](./in-game-dossier.png)
![Colonist dossier — Health](./in-game-dossier-health.png)
![Colonist dossier — Memory](./in-game-dossier-memory.png)
![Colonist dossier — Tasks](./in-game-dossier-tasks.png)
![Colonist dossier — Gear paper-doll](./in-game-dossier-gear.png)

## Design-system gallery

![Component gallery](./component-gallery.png)

## Anatomy wireframes

Schematic region maps for the dense screens (vector, lightweight).

![In-game HUD regions](./in-game-anatomy.svg)
![Party selection regions](./party-selection-anatomy.svg)
![Colonist dossier regions](./colonist-dossier-anatomy.svg)
