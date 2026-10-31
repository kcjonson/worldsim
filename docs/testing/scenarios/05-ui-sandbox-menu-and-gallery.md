# Scenario 05: ui-sandbox scene menu and gallery

**Status:** draft  
**Last verified:** never

ui-sandbox's scene menu (a salal debug layer) and its gallery scenes, worldsim's theme on salal's ui.

---

## Steps

Launch `ui-sandbox` with no `--scene`; the dev server is on 127.0.0.1:8090.

1. Look at the window: the svg scene runs, the scene menu is open in the bottom-right corner, svg selected.
2. Click `gallery` in the menu: the menu closes and the component gallery shows, dark, amber accents.
3. `/api/ui/lint`: `"count":0`.
4. Hover the MARA VANCE button: a tooltip reads "Double-click for full dossier".
5. Press F1, then the down arrow and Enter: the menu opens, focus moves down a row, and that scene starts.
6. Switch to `gallerydialog`: a modal dialog over a dimmed panel, six rows, Stone Wall selected.
7. Press Escape twice: the dialog closes; the panels underneath stay.
8. Switch to `planet` and drag with the left button: the globe orbits.
