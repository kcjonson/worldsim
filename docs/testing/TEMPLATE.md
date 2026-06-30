# Scenario NN: Title

**Status:** draft | ready | passing | failing  
**Last verified:** YYYY-MM-DD

One sentence describing what this scenario tests and why it matters.

---

## Preconditions

What must already be true before running. Either a fresh launch (default) or specific state from
a prior scenario. If this scenario depends on state from another, list the scenario number and
what specifically it must have produced (e.g. "colonist has AxePrimitive in inventory").

---

## Setup

Environment setup before the scenario steps begin. The full environment and launch procedure is
in [../README.md](../README.md); this section lists only the scenario-specific prep.

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/<verb>?<params>"
```

---

## Steps

Numbered sequence of API calls. For each step:

1. What you issue
2. What you observe or poll for (if the step is asynchronous)

```
# Step 1 — description
curl.exe "http://127.0.0.1:<PORT>/api/dev/<verb>?<params>"

# Verify
curl.exe "http://127.0.0.1:<PORT>/api/state?what=<x>"
```

---

## Expected state

The `/api/state` assertions that define a passing run. Write them as specific JSON field checks,
not vague descriptions.

```
# Example assertion (pseudocode — check these fields in the response)
GET /api/state?what=colonists
  -> colonists[0].inventory.AxePrimitive >= 1

GET /api/state?what=storage
  -> storage[0].inventory.Wood >= 200
```

---

## Pass / fail

**Pass:** all expected-state assertions hold at the end of the scenario.

**Fail:** list the specific assertion that failed and the actual value observed.

---

## Notes

Optional. Known quirks, timing sensitivities, things to watch for. If a known-open bug affects
this scenario, call it out here and link to the relevant entry in [../README.md#known-open-caveats](../README.md#known-open-caveats).
