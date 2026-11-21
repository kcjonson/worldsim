# Scenario 19: Load errors on screen

**Status:** ready  
**Last verified:** 2026-11-20

A broken config file stops the game load with the error that names it on screen, so a modder sees what is wrong without the log. A structural fault in a creature folder (a file of the wrong kind, an unreadable file) stops it at the asset check, on the splash or, after a `--scene` jump, on the loading scene; a missing kind, a bad value, or a broken behavior library entry stops it at the config load, on the loading scene. Spec: [config-loading.md](../../technical/config-loading.md#where-load-errors-surface).

---

## Steps

Edit only the staged copies beside the exe, the human's folder `build/apps/world-sim/RelWithDebInfo/assets/world/creatures/Human/` and the config folder `build/apps/world-sim/RelWithDebInfo/assets/config/`, never the source folders `assets/world/creatures/Human/` and `assets/config/`. "The staged `needs.xml`" is that file in the staged folder, "the source `needs.xml`" the one in the source folder. Launch `world-sim --background --no-settings --http-port <PORT>` (RelWithDebInfo) from `build/apps/world-sim/RelWithDebInfo/`: the game load reads `assets/config` and the human's folder from the working directory, so a launch from anywhere else reads another copy and loads cleanly. `/api/state?what=scene` reads the top scene's `failed` and `errors`.

1. In the staged `needs.xml`, set Hunger's `seek` to `"abc"`.
2. Launch the game.
3. Wait for the splash to finish: Enter Expedition is enabled.
4. Click Enter Expedition, then Quick Start: the loading scene shows a "Loading failed" panel in place of the bar.
5. Read the panel: `[needs] needs.xml: <Need name="Hunger"> seek must be a number, got "abc"` in red.
6. Read under it: "Full log:" and a path under `%TEMP%\world-sim\logs\`, which exists.
7. `/api/state?what=scene`: `failed` is true and `errors` holds the same message.
8. `/api/ui/lint`: `"count":0`.
9. Press Esc: the main menu.
10. Set Hunger's `urgent` and `critical` to `"xyz"` too.
11. Set the `urgent` of Thirst, Energy, Bladder, Digestion, Hygiene, Recreation, and Temperature to `"xyz"`.
12. Click Quick Start again: five messages, then "and 5 more", the messages scrolling inside the panel if they don't all fit.
13. `/api/ui/lint`: `"count":0`.
14. `/api/control?action=scene&scene=game`: the loading scene shows the same "Loading failed" panel.
15. `/api/state?what=scene`: it names `gameloading`, with `failed` true.
16. `/api/control?action=exit`.
17. Copy the source `needs.xml` over the staged `needs.xml`.
18. Write a staged `herd.xml` holding `<Herd/>`.
19. Launch as in step 2, with `--scene=game` added: the asset check stops the load, and the loading scene shows the "Loading failed" panel with `Human: [creatures] herd.xml: the root element <Herd> is not one a creature folder holds (<Mood>, <Memory>, <Traits>, <Attributes>, <Needs>, <creature>, <behavior>)`.
20. `/api/state?what=scene`: it names `gameloading`, `failed` is true, and `errors` holds exactly that one message.
21. `/api/control?action=exit`.
22. Launch as in step 2: the splash shows the same "Loading failed" panel with the same one message, and `/api/state?what=scene` names `splash` with `failed` true.
23. `/api/control?action=exit`, then delete the staged `herd.xml`.
24. Delete the staged `needs.xml`.
25. Launch as in step 19: the asset check passes and the config load fails; the loading scene's panel shows, and `errors` holds, exactly one message, `[creatures] creatures/Human: no file has a <Needs> root`.
26. `/api/control?action=exit`, then copy the source `needs.xml` back to the staged folder.
27. In the staged `mood.xml`, delete the `</Mood>` closing tag.
28. Launch as in step 19: the asset check stops the load; the loading scene's panel shows, and `errors` holds, exactly one message, `Human: [creatures] mood.xml: not readable XML: Start-end tags mismatch`.
29. `/api/control?action=exit`, then copy the source `mood.xml` over the staged `mood.xml`.
30. In the staged folder `config/behaviors/human/`, write a `sample.xml` holding `<behavior id="sample"><idle chain="go"/><chain id="go"><go query="nowhere"/></chain></behavior>`, and a staged `config/actions/stray.xml` holding `<Actions/>`.
31. Launch as in step 19: the asset check passes and the config load fails; the loading scene's panel shows, and `errors` holds, exactly one message, `[actions] stray.xml: the root element is not <actions>`. The broken reference in `sample.xml` is left out of it: references are resolved only once every file reads clean.
32. `/api/control?action=exit`, then delete the staged `stray.xml`.
33. Launch as in step 19: the loading scene's panel shows, and `errors` holds, exactly one message, `[behaviors] human/sample.xml: <chain id="go"> step 1 <go> query "nowhere" names no query`.
34. `/api/control?action=exit`. In the staged `sample.xml`, add `script="go.lua"` to the `<chain>`.
35. Launch as in step 19: exactly one message, `[behaviors] human/sample.xml: <chain id="go"> script must be left out; it is reserved for a Lua hook that runs nothing yet, got "go.lua"`.
36. `/api/control?action=exit`, then delete the staged `sample.xml`; the shipped behavior files stay in the folder.
37. In the staged `Human.xml`, change the `<creature>` block's `mood="true"` to `mood="maybe"`.
38. Launch as in step 19: the asset check passes (the asset registry reads nothing of the `<creature>` block) and the config load fails with exactly one message, `[creatures] Human/Human.xml: <creature> mood must be true or false, got "maybe"`.
39. `/api/control?action=exit`, then copy the source `Human.xml` over the staged `Human.xml`.
40. Write a staged `config/arbitration/rungs.xml` holding `<arbitration/>`.
41. Launch as in step 19: exactly one message, `[arbitration] arbitration: 2 files; one file declares the ladder and the bonuses (priority-tuning.xml, rungs.xml)`.
42. `/api/control?action=exit`, then delete the staged `rungs.xml`.
43. `diff -r assets/world/creatures/Human build/apps/world-sim/RelWithDebInfo/assets/world/creatures/Human` and `diff -r assets/config build/apps/world-sim/RelWithDebInfo/assets/config` print nothing.
44. Launch as in step 19: the game scene loads, and `/api/state?what=scene` names `game`.
45. `/api/control?action=exit`.
