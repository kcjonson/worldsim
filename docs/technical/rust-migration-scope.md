# C++ to Rust Migration: Scope and Spike

Created: 2026-09-25
Status: Research / exploration (no decision, no code change)
Related: [monorepo-structure.md](./monorepo-structure.md), [library-decisions.md](./library-decisions.md), [cpp-coding-standards.md](./cpp-coding-standards.md), [ecs-patterns.md](./ecs-patterns.md), [build-performance.md](./build-performance.md)

## The question

What would it take to move worldsim from C++20 to Rust? What happens to the eleven months of
work already in the tree, how big is the job, which tools exist, and how do we use AI agents to
do it without either shipping sloppy code or spending tokens like water?

Short answer: the leaf libraries (foundation, geometry, world) port cleanly and the spike below
shows AI can translate them with byte-identical behavior and no performance penalty. The middle
of the stack (ECS, UI, renderer, game scenes) is a redesign, not a translation; the borrow
checker rejects the ownership patterns that make our ECS and widget tree work today. No tool
translates C++ to Rust automatically. LLM agents are the translator, and the thing that makes
them trustworthy and cheap is a differential harness that treats the C++ build as the oracle.

## What we have today

Numbers from a census of the tree at `ac6558c`.

| Library / app | Non-test LOC | Test LOC | Port difficulty | Why |
|---|---|---|---|---|
| foundation | 4,353 | 1,826 | Low | Leaf; RNG is portable; `Arena` (no-destructor bump allocator) and `TaskPool` are the only tricky bits |
| geometry | 6,378 | 7,318 | Low | Pure functions over value types, exact integer math, best test coverage in the repo |
| world (worldgen) | 13,871 | 7,430 | Low-med | Pure pipeline stages, deterministic seeds, byte-exact `.wsplanet` I/O |
| renderer | 8,203 | 1,893 | Medium | GL state and resource lifetimes; 12 GLSL shaders |
| planet-view | 1,585 | 357 | Medium | Small, but 132 raw GL call sites |
| ui | 13,718 | 5,697 | High | Virtual widget hierarchy mixed with CRTP, parent pointers, 477 capturing lambdas, link cycle with engine |
| engine (ecs, assets, construction, nav, vision) | 39,053 | 22,927 | High | ECS aliasing (below), raw system back-pointers, sol2 bridge, largest lib |
| apps (world-sim, ui-sandbox, asset-manager, CLIs) | ~40,800 | ~190 | Med-high | Almost no unit tests of their own; world-sim alone is 29.7k lines |

Total: roughly 130k lines of non-test C++ and 47k lines of tests (2,028 gtest cases, 50 benchmarks).
Churn is high: 248 commits and +33.5k / -11.8k C++ lines in the last three months.

### The four things that make it hard

#### The ECS

`libs/engine/ecs` is a sparse-set store: one `ComponentPool<T>` per type behind a
`type_index` map, components stored by value in a dense vector with swap-and-pop removal.
`Registry::getComponent<T>()` hands out a raw `T*` whose lifetime isn't tied to anything; any
later `addComponent`/`removeComponent` can relocate the array under it. `ComponentPool.h:22-31`
already documents one production crash from this. Systems hold a raw `World*` and can mutably
alias any component at any time, and action handlers (`HaulActions.cpp`, `BuildActions.cpp`,
`CraftActions.cpp`) add and remove components while `ActionSystem` is mid-iteration. Rust won't
compile that shape. A faithful port needs `unsafe` or `RefCell` everywhere; an honest port means
re-deriving the access pattern of every system (266 `getComponent` call sites across 24 system
files) against either a Rust ECS crate or a redesigned store with deferred structural changes.

#### The engine / ui / assets link cycle

`libs/ui/CMakeLists.txt:47-54` links `engine`, and
`libs/engine/CMakeLists.txt:67-77` links `ui` and `assets`, which links back to `engine`. CMake
tolerates this. Cargo doesn't allow cyclic crate dependencies, so the cycle has to be broken
(trait inversion or merging into one crate) before the middle of the stack can be laid out as
crates at all. This is worth fixing in C++ regardless.

#### GL is everywhere

468 `gl*()` call sites: renderer 158, planet-view 132, engine 76,
ui-sandbox 73, world-sim 19, ui 8, foundation 2 (the debug server's screenshot readback). Moving
to `glow` keeps our GLSL and call shapes; moving to `wgpu` is a renderer rewrite. Either way the
calls outside `libs/renderer` have to be pulled in behind it first.

#### Exceptions and callbacks

117 throw/try/catch sites become `Result` chains, and 264
`std::function` members plus 477 `[this]`/`[&]` captures (mostly UI event handlers) need a
lifetime story. Closures that capture `this` and outlive a frame are exactly what Rust makes you
redesign.

### The things that make it easier

No C++20 concepts, no unions, no SIMD, no singletons, almost no platform code (8 `_WIN32`
sites). The 36 `reinterpret_cast`s are POD serialization guarded by `static_assert`s. The sol2
surface is 4 usertypes and a handful of functions in `libs/engine/assets/lua/`, driving 4 Lua
scripts. cpp-httplib lives only in `DebugServer`, nlohmann-json in 8 files. Threading is one
`TaskPool`. The RNGs (PCG32, SplitMix64, HashNoise) are bit-exact portable, and geometry uses
exact integer arithmetic specifically so results don't drift.

## What it means for the work so far

Most of the durable value survives a language change. The C++ source doesn't.

Carries over unchanged or nearly so:

- Design docs and specs in `docs/design/` and most of `docs/technical/`. They describe
  algorithms and data, and the recent terrain-polygons, nav, vision, and crafting specs would
  read the same in Rust.
- Assets: SVG, XML asset definitions, the Lua generators (mlua runs Lua 5.4), textures, fonts.
  The flora-asset skill keeps working.
- GLSL shaders, if we pick `glow`. With `wgpu` they'd be translated to WGSL (naga can ingest GLSL
  for the simple cases).
- The `.wsplanet` format. It's already little-endian POD with IEEE-754 asserts, so a Rust
  reader/writer can be byte-exact and the shared quickstart planet keeps loading.
- The dev-tools HTTP API on 8081/8070 and the developer-client. If the Rust debug server keeps
  the same endpoints, every curl workflow in CLAUDE.md, the screenshot and input-injection
  tooling, and the React client survive untouched. That contract also doubles as an end-to-end
  oracle (`/api/state`, `/api/ui/tree`).
- The 2,028 gtests, as a specification. Their names, inputs, and expected values port directly;
  the spike ported 74 of them with zero changes to what they assert.

Thrown away or rewritten: all C++ source, the CMake/vcpkg setup and the Windows build-performance
work (Ninja, ccache presets), `cpp-coding-standards.md`, and parts of CLAUDE.md, the skills, and
memory notes that reference C++ paths and build commands.

Changed in kind: the ECS and UI framework designs. Whatever we build there in Rust will be a new
design informed by the old one, which means the docs for those areas get rewritten, not just
their code.

## Spike: geometry core, predicates, and triangulation

To get real numbers instead of opinions, an agent ported `libs/geometry/core`,
`libs/geometry/predicates`, and `libs/geometry/triangulation` (exact `Int128` predicates and the
constrained Delaunay triangulator the navmesh is built on) to a standalone Rust crate, outside
the repo. It's a closed set: nothing else in geometry is pulled in; the only outside dependency
is `Foundation::Vec2` for quantize/dequantize.

Process, in order: inventory and hazard list, port the gtests to `#[test]` against `todo!()`
stubs, port the implementation module by module with `cargo build`/`test`/`clippy -D warnings` in
the loop, then a differential harness compiling the untouched C++ sources three ways (g++ -O2,
MSVC /O2, and one variant) and feeding both languages the same inputs from a float-free
SplitMix64 generator written identically in each.

### Results

| Measure | Result |
|---|---|
| Implementation lines (total / code) | C++ 1,746 / 1,187 to Rust 1,210 / 971 |
| Test lines | C++ 1,371 / 1,047 to Rust 1,596 / 1,424 (includes an 87-line mt19937 + libstdc++ distribution emulator so random sweeps see identical inputs) |
| Ported tests | 74 of 74, same names, inputs, and expectations; 74/74 pass first run, debug and release (independently re-run) |
| Build errors in the port | 0 across all three modules; no borrow-checker, integer-type, or API-shape errors |
| `unsafe` blocks | 0 |
| `clippy -D warnings` | clean (two lints allowed in test oracles to keep them literal to the C++) |
| Differential lines | 151,030; predicates bit-identical to g++ including f64 distances |
| Triangulation | byte-identical to C++ once the C++ flip queue is sorted (one-line change in a scratch copy) |
| Agent cost | 69 tool calls, ~280k tokens, one session; about a quarter on the port, the rest on the harness and root-causing diffs |

Perf, minimum of 7 runs, same inputs, matching checksums, no LTO, x86-64 baseline:

| Workload (ms) | Rust release | g++ -O2 | MSVC /O2 |
|---|---|---|---|
| CDT, 3,000-vertex star | 67.6 | 101.6 | 122.0 |
| CDT, 3,000 verts + 16 holes | 79.8 | 119.9 | 144.9 |
| CDT, 1,000 x 60-vertex polygons | 42.0 | 67.5 | 79.7 |
| 1M `intersectSegments` | 17.7 | 31.5 | 31.5 |
| 1M `inCircle` | 5.0 | 7.7 | 9.5 |

Not profiled. Plausible contributors are `Vec2i64` passed in registers, a 3-variant enum replacing
a 56-byte `SegmentIntersection` struct built on every call, and a cheaper hasher than
`std::hash` with prime-modulo buckets. Read it as "no Rust penalty on this code," not "Rust is
1.5x faster."

### Design changes the port made

Mostly the kind a reviewer should look at once and approve: `Int128` became native `i128` (the
MSVC two-limb path, about 150 lines, disappears); `SegmentIntersection` became an enum carrying
its point data; `SIZE_MAX` and `-1` sentinels became `Option`; the ear-clip and edge-flip lambda
clusters, which capture mutable locals that other lambdas read, became small structs with
methods; two copy-pasted distance predicates merged into one returning `Ordering`. Invalid input
still returns an empty `Vec`; switching to `Result` is an API decision, not a translation.

The closure-to-struct changes were designed before any code was written, which is why the
borrow checker never fired. That's the lesson for the hard modules: decide the ownership shape
first, then translate.

### What the spike found in the C++

#### The triangulator isn't deterministic across compilers

`Triangulation.h:20` says "Output is
deterministic for a given input." It seeds the Lawson flip queue by iterating a
`std::unordered_map` (`Triangulation.cpp:661`), so the order depends on the standard library.
g++ and MSVC builds disagree on raw triangle order in about 90% of cases and on the triangle set
in 17 of 3,000, all on exactly cocircular quads where both answers are valid CDTs. macOS (libc++)
would be a third answer. This matters for anything that assumes matching navmeshes across
machines: multiplayer, replays, cached meshes. Sorting the queue fixes it in C++ today.

Smaller findings: MSVC's two-limb `Int128::toDouble` rounds twice, giving 1-ULP differences on
crossing points past 2^53 mm (far outside region-local nav coordinates); the `mergeHoles`
fallback uses an unstable `std::sort` over equal bridge lengths, the same bug class as the flip
queue; `angleLess` and `closerThanToSegment` have no gtest coverage.

None of the 74 unit tests could see any of this. They'd have passed a port that silently changed
triangle order. The differential harness found every real issue, and it settled one equivalence
question ("does this simplification change vertex rotation?") in a single run.

### How far the spike generalizes

Not very, in the direction that matters. This slice is the best case: pure functions, value
types, no inheritance, no globals, no threads, no FFI, strong tests. The zero borrow-checker cost
here will not repeat in the ECS, the widget tree, or anything holding GL resources. What does
generalize is the process and the ratio: translation is cheap, verification is where the effort
goes, and verification effort is reusable if the harness is built as shared infrastructure.

## Tools

### Translators

There is no C++-to-Rust translator. c2rust is C only and emits unsafe, unidiomatic Rust. DARPA's
TRACTOR program is also C only and still research, with no published field-ready success rates.
The academic LLM translators (Syzygy, RustMap, VERT, RustPrint) all target C, on repos of 7k to
84k lines, with self-reported numbers. Templates, overloading, RAII, inheritance, and exceptions
are unsolved by any of them.

Production-scale AI migrations do exist, just not C++ to Rust:

- Bun ported 535,496 lines of Zig to Rust with 64 parallel Claude agents in 11 days, 6,500
  commits, $165k in API cost (5.9B uncached input tokens, 690M output, 72B cached reads). The
  team's stated preconditions: an engineer who knows the codebase deeply, an "extremely robust"
  test suite, and willingness to spend tokens speculatively.
  [Pragmatic Engineer](https://blog.pragmaticengineer.com/the-pulse-what-can-we-learn-from-buns-rapid-rust-rewrite-with-ai/)
- Google reports 39 internal framework migrations in 12 months where LLMs authored about 70% of
  edits and engineers estimated a 50% time saving.
  [arXiv 2504.09691](https://arxiv.org/abs/2504.09691)
- Microsoft has a research goal of eliminating C/C++ by 2030 with agent tooling; it's explicitly
  a research project, not a Windows rewrite.
  [The Register](https://www.theregister.com/2025/12/24/microsoft_rust_codebase_migration/)

Bun's cached-read count is the number worth staring at: 12x more cached input than uncached.
The economics of large migrations come from a big, stable, shared prefix (rules, APIs, context)
reused across thousands of agent turns.

### Interop, for incremental migration

- [cxx](https://cxx.rs) is the mature choice (Chromium, Android): a declared bridge module with
  safe `Vec`/`String`/`CxxVector` types. Templates and callbacks need hand-written shims.
- autocxx generates cxx bridges from existing headers; the Google repo is archived and a
  community fork carries on.
- bindgen/cbindgen for C-shaped boundaries only.
- Crubit (Google) is the most ambitious and the least usable outside Bazel.
- [Corrosion](https://corrosion-rs.github.io/corrosion/) builds Cargo crates inside CMake; its
  cxx/cbindgen header generation is still experimental.

Our leaf APIs are FFI-friendly: geometry and worldgen take and return value types and vectors of
them. The engine/ui boundary is the opposite; it's objects, callbacks, and back-pointers.

### Dependency map

| Today | Rust | Notes |
|---|---|---|
| GLFW + GLEW + OpenGL | winit + glow, or wgpu | glow keeps GLSL and call shapes; wgpu is the modern route and a renderer rewrite |
| glm | glam | Mature, SIMD, glm-inspired |
| nlohmann-json | serde_json | No gap |
| stb_image | image | No gap |
| cpp-httplib | tiny_http or axum | tiny_http matches the blocking debug-server shape |
| nanosvg | usvg | Far more complete; keep our tessellator or evaluate lyon |
| our tessellator | port, or lyon | lyon is mature; ours is a libtess-style sweep-line with known behavior |
| msdfgen | msdfgen-rs, fdsm | Weakest link; fdsm isn't feature-complete |
| pugixml | roxmltree or quick-xml | No gap |
| Lua 5.4 + sol2 | mlua | Closest match to sol2, maintained |
| our ECS | port, hecs, or bevy_ecs | hecs is a library; bevy_ecs pulls in Bevy's scheduling model |
| gtest | `#[test]`, cargo-nextest, insta, proptest | No gap |
| Google Benchmark | criterion or divan | No gap |

### Quality tooling

`cargo clippy -D warnings`; miri for any `unsafe` (the spike needed none); cargo-geiger to count
`unsafe` per crate as a tracked metric; cargo-mutants to check that ported tests actually fail
when the code is wrong (the spike's unit tests would have missed a triangle-order change);
proptest for the geometry invariants the validator checks. None of these is validated by the
migration literature specifically; they're standard Rust practice.

### Rust gamedev reality check

The most-cited cautionary account is LogLog Games' "Leaving Rust gamedev after 3 years"
([link](https://loglog.games/blog/leaving-rust-gamedev/)): a two-person indie studio with 100k+
lines of Rust, whose complaints were iteration speed, ecosystem churn, and the borrow checker
fighting fast experimentation. The counterweight is Tiny Glade (Bevy-based, shipped, successful)
and Veloren's long-running wgpu codebase. Hot reload of Rust game logic works through a dylib
split (hot-lib-reloader-rs) and is more manual than people expect. Our asset hot-reload is data
level and unaffected.

The LogLog complaint lands closest to us. We're a small team iterating fast on gameplay (248
commits in three months), and the areas under the heaviest churn (colonist tasks, crafting,
construction, UI) are the ones the borrow checker will push back on hardest.

## Migration strategies

Ranked by how well they fit this codebase.

#### 1. Leaves first, then decide

Port foundation, geometry, and world to Rust crates and ship
worldgen-cli as a pure Rust binary. worldgen-cli depends only on world and foundation, so this
needs no FFI at all, and `.wsplanet` byte-equality against the C++ bake is a complete end-to-end
oracle. Then, and only then, decide on the middle: either expose the Rust leaves to the C++ game
through cxx (strangler fig) or stop there. About 24.6k of the 130k non-test lines, with 16.6k
lines of tests to port as the oracle. This is the only option where most of the work stays
useful if we stop halfway.

#### 2. Strangler fig across the whole game

Leaves as above, then pull modules across the cxx
boundary one at a time. Works for geometry and worldgen, whose APIs are value types. Gets ugly
at engine and ui, where the boundary would cross the ECS and the widget tree. Our own CLAUDE.md
rules help here: "replace, don't layer" means each step deletes the C++ module in the same PR,
so the only second path at any time is the FFI shim itself. The middle of the stack would likely
still end up as one large move (engine + ui + renderer + apps together) because the link cycle
and the ECS aliasing don't split cleanly.

#### 3. Bun-style big bang

Freeze features, fan out agents over the whole tree, fix until tests
pass. We fail Bun's second precondition: the apps have about 190 lines of tests for 40.8k lines
of code, and world-sim (29.7k) has none. For the libraries it's plausible; for gameplay it would
mean trusting manual play-testing as the oracle.

#### 4. Stay in C++ and take the lessons

Fix the flip-queue determinism, break the
engine/ui/assets cycle, make ECS structural changes deferred so `getComponent` pointers can't be
invalidated mid-iteration, pull stray GL calls behind the renderer. Every one of those is also a
prerequisite for options 1 through 3, so none of it is wasted if we migrate later.

## Using AI to do it without burning tokens

A migration of this size will spend a lot of tokens no matter what; the spike used about 280k on
3.1k lines, three quarters of it on verification. The goal is spending them where a model's
judgment is needed and nowhere else. What that looks like in practice:

1. Gates check, agents don't. Compiler, clippy, tests, and the differential harness are
   free and exact. An agent should spend tokens only when a gate fails, never "reviewing" code by
   reading it. Every port task ends in a mechanical pass/fail.
2. Build the differential harness once, as shared infrastructure. The spike spent about 60%
   of its effort on a one-off harness. A reusable shared-seed generator (C++ and Rust), output
   canonicalizers, and a CMake target that builds C++ oracle binaries would make each later module
   cost closer to the port-only quarter. For worldgen, `.wsplanet` byte comparison is the harness.
3. Write the porting rulebook once and cache it. A short doc of translation decisions made by
   a human (Int128 to i128, exceptions to `Result` with `thiserror`, `LOG_*` to `tracing`,
   sentinels to `Option`, `std::function` members to boxed `FnMut` or a message enum, determinism
   rules, overflow policy) goes into every agent's prompt as a fixed prefix. Bun's 72B cached
   reads are this pattern at scale; cached input costs a tenth of fresh input.
4. Pack context per module, don't explore. Each agent gets the module's `.h`/`.cpp`/`.test.cpp`,
   the public Rust API of its already-ported dependencies (signatures only), the rulebook, and the
   census hazards for that module. No repo-wide search. Port in dependency order so there's always
   a finished API to pack.
5. Grep for determinism hazards before porting. Unordered-container iteration, unstable sorts,
   unspecified argument evaluation order, `std::uniform_*_distribution`, `#if` platform paths.
   Finding them with grep is nearly free; finding them through differential diffs cost the spike
   most of its root-causing budget.
6. Route by difficulty. Mechanical tiers (tests, math types, serialization, json, logging,
   leaf modules with strong tests) go to a cheaper model. Design tiers (ECS store, UI ownership
   model, renderer resource lifetimes) and differential root-causing go to the strongest model,
   and start with a written ownership design that a human approves before any translation.
7. Fan out by DAG layer. Parallel agents in worktrees only for modules at the same layer, so
   nobody ports against an API that's still moving.
8. Cap the repair loop. If a module hasn't converged after a fixed number of build/test
   rounds, stop and escalate to a human. Runaway repair loops are where token budgets go to die.
9. Review design changes, not diffs. Each agent emits a "design changes" list like the one
   in the spike above (API reshapes, redefined equivalence, dropped code, test adaptations). The
   human reviews that list, plus any `unsafe`, instead of reading every translated line.
10. Track quality as numbers in CI. gtests ported vs total, differential pass rate, `unsafe`
    count, clippy clean, mutation score on ported modules. A regression in any of them blocks the
    merge.

## Open questions for a go/no-go

- Is cross-platform determinism a product requirement (multiplayer, replays)? If yes, the C++
  has latent bugs today and the case for Rust's stricter defaults gets stronger; the fixes are
  needed either way.
- Are we willing to redesign the ECS? The ECS is the gate for everything above the leaves.
  Porting it as-is means pervasive `unsafe`; adopting hecs or bevy_ecs changes how every system
  is written.
- glow or wgpu? glow is the smaller step; wgpu is where the Rust ecosystem is going and the
  bigger rewrite.
- How much feature velocity are we willing to trade? Option 1 can run alongside feature work
  because worldgen churns less than gameplay. Options 2 and 3 compete directly with it.
- What's the stop rule? Option 1's go/no-go signals would be: worldgen-cli produces a
  byte-identical `.wsplanet`, worldgen bake time is no worse, zero `unsafe` outside FFI, and the
  per-module effort after the first one drops the way the harness argument predicts.

## Recommendation

Don't commit to a full migration on this evidence. The evidence says the easy third ports
cleanly and cheaply and the hard two thirds is a rewrite of our ECS and UI, which is a design
decision about the game more than a language decision.

Do option 4's fixes now regardless (they're prerequisites for any path and bugs on their own),
starting with the triangulator's flip-queue sort. If we want a real answer on Rust, option 1 is a
bounded experiment with a hard oracle: port the leaves, ship worldgen-cli in Rust, measure the
per-module cost with the shared harness in place, and decide on the middle of the stack with
real numbers.

The spike crate and harness were scratch work and are not checked in, per the no-scripts-in-docs
rule; the numbers above are the record.
