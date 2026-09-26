# Rust Rewrite Plan

Created: 2026-09-25
Status: Plan (decision made 2026-09-25: rewrite the whole codebase in Rust, agent-driven; no implementation yet)
Scope and spike: [rust-migration-scope.md](./rust-migration-scope.md)
Renderer: rendering/vulkan-migration.md (in PR #271, not yet merged)
Related: [build-performance.md](./build-performance.md), [../testing/README.md](../testing/README.md)

## Decision

We rewrite worldsim in Rust with Claude agents doing the translation, and we do it now, while
the codebase is 130k lines rather than 300k. The rewrite is a faithful translation everywhere
except three redesign zones, each governed by a written contract:

- Renderer: OpenGL becomes Vulkan 1.4 through `ash` (or wgpu, settled by the #271 spike), per
  the renderer contract in the Vulkan doc.
- ECS: sparse-set storage kept, raw `T*` access replaced by borrow-checked views, structural
  changes deferred through a command buffer, systems take `&mut World` as a parameter.
- UI: data inheritance becomes composition (`ComponentBase` field plus a `Component` trait),
  `[this]` callbacks become message enums dispatched to the owner.

Naming follows Rust conventions (snake_case, mandatory `self.`), per the code-style section of
the scope doc. The C++ tree stays the shipping build and the behavioral oracle until the Rust
build reaches parity, then it's deleted in one change. No FFI, no C++ and Rust in one binary.

This doc gives no time or dollar estimates. The pilot in Phase 1 measures tokens per thousand
lines per tier on our code with the real pipeline, and the budget is set from that measurement
before the fan-out starts.

## What the precedents say

Four efforts are close enough to steal from. Every claim below was checked against its primary
source.

**Bun, Zig to Rust** ([Bun blog](https://bun.com/blog/bun-in-rust),
[Pragmatic Engineer](https://blog.pragmaticengineer.com/the-pulse-what-can-we-learn-from-buns-rapid-rust-rewrite-with-ai/)).
535,496 lines of Zig, 1,448 files, 11 days, 64 concurrent Claude agents across 4 worktrees,
6,502 commits, 5.9B uncached input tokens, 690M output, 72B cached reads, about $165k. Phases:
a porting guide written in about three hours of conversation, a lifetimes map generated and
cross-reviewed before any code, a 3-file trial, mechanical translation of every file, then about
16,000 `cargo check` errors worked crate by crate (about 100 crates), each by one implementer and
two adversarial reviewers. Zero tests skipped or deleted. 19 regressions after merge, nearly all
"same syntax, different semantics" (`debug_assert!` hiding a side effect, `cast_slice` panicking
where Zig truncated). About 4% of the result is `unsafe`. Agents collided on `git stash` and
`git reset` until those were banned. Critics point to a long stabilization tail after the 11 days.

**GitHub Copilot runtime, TypeScript to Rust**
([GitHub blog](https://github.blog/ai-and-ml/generative-ai/migrating-the-github-copilot-runtime-to-rust-using-copilot/)).
About 430k lines of TypeScript to 832k lines of Rust plus 469k lines of Rust tests, 14.5 weeks,
one engineer, 128 port PRs, 135 releases shipped during the port. Incremental: each PR replaced a
module with a shim into Rust and deleted the TypeScript in the same change. 96.2% of input tokens
were cache reads. Of 8,678 rustc errors, 37% were name resolution and 1.7% were ownership,
borrowing, or lifetimes, which matches our spike (zero borrow errors on value-type code).
Regressions clustered in ambiguous semantics, ambient runtime behavior, paired operations that
fell out of sync, and lifecycle/ownership; one agent deleted end-to-end tests without permission.
158 `unsafe` blocks, all at real FFI or OS boundaries.

**fish shell, C++ to Rust** ([fish blog](https://fishshell.com/blog/rustport/)). The human-driven
precedent at our scale: about 57k lines of C++ to 75k lines of Rust over roughly a year,
component by component through autocxx, shipping a C++ release mid-port. Their hardest part was
"the second 90%": exhaustive testing surfaced latent bugs the port exposed rather than caused.
Big entangled subsystems ended up ported in large chunks because fine-grained FFI cost too much.

**Anthropic's migration methodology** ([claude.com/blog/ai-code-migration](https://claude.com/blog/ai-code-migration)).
Six steps: rulebook and dependency map, stress-test the rules on a small batch with three agents
(rule follower, senior engineer, rule writer), translate everything with smaller models while
larger ones review adversarially, then compile, run, and match behavior with the same loop. The
work queue is rebuilt from disk state so runs are resumable by construction. "You fix the process
that produced the code," not individual failures: when a rule changes, regenerate only the files
the rule touched.

Two counter-lessons: Tor abandoned incremental C-to-Rust replacement because its modules weren't
separable, and TypeScript's Go port defined success as diffing output against the original on a
20,000-case corpus rather than passing hand-written tests. Nobody has published an AI rewrite of
a C++ game engine, so we have no domain precedent; our oracles have to be built, not assumed.

### What that means for our strategy

Bun and Copilot took opposite routes. Copilot's incremental shims worked because each TypeScript
module had a clean call boundary into Rust. Ours don't: the census found an engine/ui/assets link
cycle, ECS pointer aliasing across every system, and a renderer whose API changes (GL to Vulkan)
so there's nothing stable to shim into. That's Tor's situation, and FFI across the ECS would be
the costliest code in the project and all of it thrown away.

So the plan is **layered, dependency-ordered, and FFI-free**: a separate Rust workspace ported
bottom up (the ORBIT paper's topological order, not Bun's translate-everything-then-fix), where
each layer must pass its oracles against the C++ build before the next layer starts. The C++ game
keeps shipping until Rust reaches parity. It borrows Bun's machinery (porting guide, trial batch,
adversarial review, crate-grouped repair, git discipline) and Copilot's economics (cache-heavy
sessions, cheap models for exploration, "one path" deletion at cutover).

## Oracles: what "equivalent" means

The existing gtests carry us through the libraries. The app layer needs oracles built first.

| Layer | Oracle | Exists today? |
|---|---|---|
| foundation, geometry | Ported gtests (same names, inputs, expectations) plus shared-seed differential harness vs C++ oracle binaries | gtests yes; harness proven in the spike |
| world (worldgen) | Ported gtests plus the golden full-pipeline `worldHash` gate (`worldgen-cli --expect-hash`) and byte-equal `.wsplanet` | yes, both |
| engine systems | Ported gtests (11 of 17 systems covered) plus tick-by-tick sim replay: fixed seed, fixed timestep, scripted input, `/api/state` snapshots compared at tick N | gtests yes; replay no |
| gameplay end to end | The four HTTP scenario tests in `docs/testing/scenarios/`, automated as scripts that run against any build | scenarios yes (manual); automation no |
| UI layout | `/api/ui/tree` element bounds and `/api/ui/lint` zero violations, compared scene by scene | endpoints yes; comparison no |
| rendering | Golden images from the GL build, compared with NVIDIA FLIP (`nv-flip-rs`) under a perceptual threshold; Rust side renders headless on lavapipe | no (#271 prep) |
| performance | Worldgen bake time, frame time on fixed game states, groundcover GPU time (~1.75 ms today) | partly (perf-capture) |

Oracles only count if the C++ is deterministic. The spike found the triangulator isn't across
standard libraries (WOR-466). Phase 0 sweeps for that bug class and proves determinism by running
each C++ oracle twice, and on two compilers, before anything is ported against it.

## Phases

Each phase ends at a mechanical gate plus a human sign-off. Each automated phase runs as its own
workflow, since workflows can't take mid-run input.

### Phase 0: C++ prep (oracles and freeze)

Human-led with agents, on the local machine. Everything here improves the C++ build too.

- Fix WOR-466 (sort the flip-queue seed; stable sort in the `mergeHoles` fallback).
- Determinism hazard sweep by grep: unordered-container iteration feeding output, unstable
  sorts, unspecified argument evaluation order, `std::*_distribution`, platform `#if` paths.
  Fix each in C++ or record it as an allowed divergence.
- Sim determinism: fixed-timestep sim tick, one seeded RNG path, no wall clock in sim code, and a
  headless sim mode (ECS plus systems, no window) so replays run on a Linux box with no GPU.
- Replay recorder: scripted dev-API input plus `/api/state` snapshots at fixed ticks; proven
  deterministic by running twice on MSVC and GCC builds.
- Automate the four scenario tests as scripts against the dev-tools API.
- Fill the six untested systems: `BuildGoalSystem`, `StorageGoalSystem`, `NeedsDecaySystem`,
  `PhysicsSystem`, `TimeSystem`, `DynamicEntityRenderSystem`.
- Defer ECS structural changes in C++ (command buffer applied after system iteration). This
  changes behavior ordering, so it lands in C++ first; otherwise the C++ oracle disagrees with
  the Rust ECS contract by design.
- #271's prep: screenshot readback before swap, golden image set on the local GPU.
- Differential harness as reusable infrastructure: the spike's float-free SplitMix64 generator in
  both languages, output canonicalizers, a CMake target that builds per-module C++ oracle binaries.
- Freeze: decide what happens to in-flight epics (open decision 3), then freeze C++ feature work.
  Bug fixes that improve an oracle are still allowed.

Gate: every oracle in the table above exists (except rendering goldens for scenes not yet built),
runs green on C++, and is deterministic across two runs and two compilers.

### Phase 1: rulebook, contracts, run infrastructure, pilot

Human-heavy. Anthropic's advice and Bun's experience agree: front-load human hours here.

- `PORTING.md`, the rulebook: naming, type map (`Int128` to `i128`, `glm` to `glam`, sentinels to
  `Option`), error policy (exceptions to `Result` with `thiserror`), overflow policy
  (`overflow-checks` on in release for geometry), `LOG_*` to `tracing`, Args structs with
  `..Default::default()`, determinism rules, `unsafe` allowlist, crate and module layout.
- Contracts: renderer (from #271), ECS, UI, each with the before/after shapes from the scope doc.
- Crate map: the workspace mirrors `libs/`, with the engine/ui/assets cycle broken by trait
  inversion. Write it down before any code, since Cargo rejects cycles outright.
- Stress test on one small batch (a foundation module, one ECS system, one UI widget) with the
  three-agent pattern; revise the rulebook until the three agree.
- Spikes: #271's Vulkan vertical slice (ash vs wgpu), an ECS spike porting `ActionSystem` plus
  two simpler systems onto the contract, a UI spike porting `Button` and `CraftingDialog` onto
  messages.
- Run infrastructure (see [Where it runs](#where-it-runs)): run host, sccache, mold,
  cargo-nextest, a frozen run-settings profile, the work-queue and gate scripts, and the
  context-pack generator.
- Pilot: port all of `libs/foundation` with the real pipeline. Record tokens per thousand lines
  per tier, repair rounds, gate pass rates, and review findings.

Gate: rulebook and contracts signed off, spikes decided, pilot numbers in hand, budget set.

### Phase 2: leaves (automated)

`geometry` and `world`, then `worldgen-cli` as a pure Rust binary. No GPU, no FFI.

Gate: all ported gtests pass (count equals the C++ count, no skips), differential harness green
per module, `worldHash` matches, `.wsplanet` byte-equal to the C++ bake, bake time no worse.

### Phase 3: engine core, headless (automated, design-zone review)

ECS per contract, all systems, nav, vision, construction, assets with Lua through `mlua`, config
loading, and the dev-tools HTTP server (`tiny_http`) in a headless Rust build.

Gate: ported gtests, replay snapshots equal to C++ at every checkpoint tick, all four scenario
tests pass against the headless Rust build over the unchanged HTTP API.

### Phase 4: renderer, UI, apps (automated, heavy review)

Vulkan renderer per contract, `ui` per contract, `planet-view`, world-sim scenes, ui-sandbox,
asset-manager, the CLIs.

Gate: goldens within FLIP threshold under lavapipe, `/api/ui/tree` bounds match per scene with
zero lint violations, scenario tests pass on the full game, perf thresholds met.

### Phase 5: parity and cutover

`docs/testing/regressions.md` checklist, a manual play session, Mac smoke test through MoltenVK,
then one change that deletes the C++ tree, CMake, and vcpkg, and rewrites CI. Replace
`cpp-coding-standards.md` with a Rust standards doc; update CLAUDE.md, skills, and memory notes
that reference C++ paths. Development log entry.

## The loop

One shape, reused in every automated phase. Agents never decide that something is done; gates do.

1. Queue from disk. A script derives each work item's state from files: the Rust module
   exists, its gate results file exists and is green. Rerunning the script after any crash or
   pause rebuilds the queue. No state lives in a conversation.
2. Pack. A script builds the item's context pack: the module's C++ `.h`/`.cpp`/`.test.cpp`,
   the public signatures of its already-ported Rust dependencies (from rustdoc JSON), the
   rulebook, the relevant contract, and that module's census hazards. Include graphs and
   signatures on the C++ side come from `clang -Xclang -ast-dump=json` against
   `compile_commands.json`.
3. Translate. One implementer ports tests first against `todo!()` stubs, then the module.
4. Gate. Scripts run `cargo check`, `clippy -D warnings`, `nextest`, the module's differential
   run, and a test-parity check (test names and assertion counts vs the C++ file, so a deleted or
   weakened test fails the gate).
5. Repair. Failures go back to the implementer with the gate output only, capped at a fixed
   number of rounds. Past the cap the item escalates (stronger model once, then a human).
6. Review. Design-zone items and anything touching `unsafe` get two adversarial reviewers with
   separate contexts; disagreement goes to a third. Mechanical items get sampled review.
7. Commit. One item, one commit, from the item's own worktree.
8. Fix the loop. When a failure class repeats, change the rulebook or a codemod and regenerate
   only the items that rule touches.

Agent rules, enforced by permissions and hooks, not by trust: no `git stash`, `git reset`, or
multi-file git operations; never delete or skip a test; never edit a gate script or the rulebook;
no `unsafe` outside allowlisted modules; no explanatory comments covering for stubbed code
(Bun's agents did this).

## Where it runs

| Host | What it is | Use it for |
|---|---|---|
| Local desktop (Windows, RTX 3090) | Interactive Claude Code, the Desktop app, Remote Control to steer from the phone | Phase 0 and Phase 1 work, spikes, human sign-offs, golden capture on the GPU |
| Run host (big Linux box: WSL2 on the desktop, or a rented many-core VM) | Claude Code in a long-lived session with Remote Control, workflows sized to the core count | The Phase 2 to 4 fan-outs, where many agents run `cargo` concurrently |
| Anthropic cloud sessions | Isolated Ubuntu 24.04 VM per session: 4 vCPU, 16 GB RAM, 30 GB disk; Rust, GCC, Clang, CMake, Ninja preinstalled; crates.io and rustup allowlisted; setup script cached when it finishes in about 5 minutes | Parallel single-crate jobs (one VM each), adversarial review, nightly and weekly gate runs as routines, auto-fix on PRs |
| Self-hosted environment | Cloud sessions on our own runners | Only on Team or Enterprise plans (public beta); on Max, the run host with Remote Control is the equivalent |

Why not run the fan-out in Anthropic's cloud: a 16-agent workflow means up to 16 concurrent
`cargo` builds on one VM, and 4 vCPU with 30 GB of disk (C++ oracle build, vcpkg, a Rust target
dir per worktree, the 1.3 GiB planet) won't hold it. Cloud sessions scale out instead: each
`claude --cloud` task gets its own VM, and a [project](https://code.claude.com/docs/en/claude-projects)
can coordinate many of them. That fits per-crate work in Phases 2 and 3, as long as a crate plus
its dependencies builds comfortably on 4 cores.

Workflow results survive a cloud VM being reclaimed; relaunching the workflow returns completed
agents from cache and reruns the rest. Background shells and subagents that were mid-run don't
survive, which is another reason the queue lives on disk.

### What `/loop` is for

Not for waiting on workflows; the harness already notifies when one finishes. `/loop` in
self-paced mode is the conductor between workflows, watching state the harness can't see:

- CI and auto-fix status on phase PRs.
- Results of routines (nightly full gate, weekly `cargo-mutants`, `miri` on `unsafe` modules).
- Usage-limit resets on a subscription plan, to relaunch a paused phase.
- The queue script's output: when a layer's gate goes green, start the next layer's workflow;
  when an item escalates, file it in Specboard and ping the phone.

Each tick reads status files and advances at most one thing, with long sleeps (20 to 30 minutes)
when nothing's pending. The workflow does the work, the loop keeps the pipeline moving, and the
human signs off at phase gates.

Routines (scheduled cloud agents) cover the recurring checks without the run host: a nightly job
that builds the Rust workspace and runs the full gate suite, a weekly mutation-testing and `miri`
job, and auto-fix watching open phase PRs.

## Token efficiency

Verified prices per million tokens
([pricing](https://platform.claude.com/docs/en/about-claude/pricing)):

| Model | Input | Output | Cache read | 5 min / 1 h cache write | Batch in / out |
|---|---|---|---|---|---|
| Fable 5.1 | $10 | $50 | $0.25 | $12.50 / $20 | $5 / $25 |
| Opus 5.5 | $4 | $20 | $0.20 | $5 / $8 | $2 / $10 |
| Sonnet 5 | $2 | $10 | $0.20 | $2.50 / $4 | $1 / $5 |
| Haiku 4.5 | $1 | $5 | $0.10 | $1.25 / $2 | $0.50 / $2.50 |

Cache reads cost about the same on every model, so the price gap between models lands almost
entirely on output and uncached input. Agent loops are cache-dominated (Bun: 72B cached reads
against 5.9B uncached; Copilot: 96% cache reads), which shapes every lever below.

1. Scripts before tokens. Anything a rule can do runs as a script: `cargo fix`,
   `clippy --fix`, `rustfmt`, ast-grep codemods for recurring rewrites, the gates themselves, the
   queue, the context packs. An LLM edit that a codemod could make is waste.
2. One stable prefix. The rulebook, contracts, and a minimal tool set form an identical prefix
   for every agent of a tier, so fan-outs share cache (workflows hold sibling agents briefly so
   they read the first agent's cache). Freeze CLAUDE.md and the run-settings profile for the
   length of a phase, since editing either invalidates every cached prefix. Strip MCP servers the
   run doesn't use; the computer-use toolset alone is about 4,500 tokens per request.
3. Cache TTL. Workflow agents cache for 5 minutes by default. Where agents of the same shape
   start more than 5 minutes apart, `subagentPromptCacheTtl: 1h` pays for itself after two reads.
4. Route by tier.

   | Work | Model | Why |
   |---|---|---|
   | Error clustering, test-parity triage, queue bookkeeping | Haiku 4.5 | Classification; short outputs |
   | Mechanical translation (tests, leaves, most systems) and repair | Sonnet 5 | Volume; Anthropic's and Bun's implementer choice |
   | Design zones (ECS, UI, renderer backend), differential root-causing, adversarial review | Opus 5.5 | Judgment; cache reads barely cost more than Sonnet's |
   | Rulebook and contract authoring, escalations past the repair cap | Fable 5.1 | Rare and high-stakes; keep outputs to verdicts and diffs |

5. Short, structured outputs from expensive models. Reviewers return schema-checked verdicts
   (pass, or a list of defects with locations), not rewritten code. Output is the most expensive
   token on every model.
6. Tests first, gates always. The spike spent three quarters of its tokens on verification
   because the harness was one-off. Built once in Phase 0, the harness turns verification into
   free script runs, and agents spend tokens only on failures.
7. Fix the loop, not the file. A rule change regenerates only the items it touches. Brute
   retries (Airbnb's lesson) beat hand-tuning one item's prompt, within the repair cap.
8. Batch API, selectively. For no-tool transformations such as first drafts of gtest-to-Rust
   test files, the Batch API is half price and stacks with caching. It adds a second pipeline, so
   use it only if the pilot shows first drafts pass the gate often enough to matter.
9. Measure before spending. The `/workflows` view reports tokens per agent. The pilot's
   numbers per tier become the phase budgets; a phase that runs past its budget stops for review.

### Subscription or API

On a claude.ai plan, workflows in an interactive session pause at a usage limit and resume after
the reset (not in `claude -p`, background, or Remote Control sessions), cloud sessions draw on the
same limits, and there's no separate VM charge. That caps throughput at the weekly limit but keeps
spend fixed. API billing removes the cap and bills every token. A reasonable split: subscription
for Phases 0 and 1 and for routines, and a decision after the pilot on whether the Phase 2 to 4
fan-outs run on the API.

## Quality bar

Enforced by gates on every item and every phase:

- `cargo clippy -D warnings`, `rustfmt --check`, `cargo-deny`.
- Test parity: every C++ test has a Rust test with the same name; assertion counts don't drop.
- `unsafe` only in allowlisted modules (renderer backend, arena), counted with cargo-geiger and
  reviewed; `miri` on those modules weekly.
- Differential and replay oracles green; goldens within threshold; scenario tests pass.
- `cargo-mutants` weekly on ported modules, so tests that pass without testing anything show up.
- A targeted test list for the regression classes every precedent hit: debug-only asserts with
  side effects, truncation vs panic, integer width and signedness, overflow wrap vs panic,
  iteration order, stable vs unstable sort, float formatting and rounding.

## Risks

- The sim isn't deterministic yet. If Phase 0 can't make replays repeat, gameplay equivalence
  falls back to the scenario tests alone, which is a much weaker oracle. This is the plan's
  biggest risk and the first thing Phase 0 proves or disproves.
- Stabilization tail. Bun's 11 days were followed by months of fixes, and fish's hardest part
  was the last stretch. Phase 5 is real work, not a formality.
- Compiler divergence in the oracle. The Linux run host builds the C++ with GCC and libstdc++,
  whose container iteration and sort ties differ from MSVC. The hazard sweep covers it; two-compiler
  determinism in the Phase 0 gate proves it.
- Freeze cost. Every week of freeze is a week without gameplay work; every week without a
  freeze is churn the port has to chase. The layered plan lets Phase 2 overlap with feature work
  outside `libs/foundation`, `libs/geometry`, and `libs/world`.
- Agents gaming gates. Copilot's run had an agent delete tests; Bun's hid stubs behind long
  comments. Test-parity gates, protected gate scripts, and permission denies handle the known
  forms; adversarial review handles the rest.
- `unsafe` creep. Bun landed at 4%. Our allowlist keeps it to the renderer backend and the arena.
- Rate limits. A subscription run pauses at limits; plan phases around weekly resets or move
  the fan-outs to the API.

## Open decisions

1. Plan and billing. Max or Team decides whether self-hosted runners exist; subscription or
   API decides whether the fan-outs are throttled or metered.
2. Run host. WSL2 on the desktop (free, shares the GPU machine with daily work) or a rented
   many-core Linux VM (isolated, costs money).
3. In-flight epics. Organic terrain (WOR-455) is mid-development and touches worldgen, nav,
   and rendering; finish it in C++ before the freeze, or pause it and build it in Rust after parity.
   UI spec alignment (WOR-446) folds into the UI contract. The remaining build-speed work
   (WOR-436) mostly dies with CMake.
4. ash or wgpu. Settled by the #271 spike.
5. ECS substrate. Port our own sparse set onto the contract, or adopt `shipyard` (the closest
   sparse-set crate; its maintainer has hedged on long-term commitment) or `hecs`/`bevy_ecs`
   (archetype-based). Settled by the ECS spike.
6. Freeze date. Set once Phase 0's scope is known.
