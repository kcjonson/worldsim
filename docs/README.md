# Documentation

Project documentation for World-Sim organized by purpose and audience.

---

## Quick Start

**New to the project?** Start here:

1. Read the main [README.md](../README.md) for build instructions
2. Check [status.md](./status.md) to see what we're working on
3. Review [setup.md](./setup.md) for development environment setup
4. Consult [workflows.md](./workflows.md) for common tasks

**Working on a feature?**

1. Check [design/INDEX.md](./design/INDEX.md) for game design requirements
2. Check [technical/INDEX.md](./technical/INDEX.md) for implementation details
3. Check [design/mvp-scope.md](./design/mvp-scope.md) for what's in scope
4. Update [status.md](./status.md) when done

---

## Directory Structure

```
docs/
├── README.md                    # This file - navigation hub
├── status.md                    # Current project status
├── setup.md                     # Development environment setup
├── workflows.md                 # Common development tasks
│
├── design/                      # Game design documents
│   ├── INDEX.md                # Master list of design docs
│   ├── mvp-scope.md            # Single source of truth for MVP
│   ├── game-overview.md        # Core game concept
│   ├── visual-style.md         # Art direction
│   │
│   ├── game-systems/           # Game mechanics organized by topic
│   │   ├── colonists/          # Colonist behavior, needs, AI
│   │   ├── world/              # World mechanics, rooms, entities
│   │   └── features/           # Discrete player features
│   │
│   └── research/               # Market research, competitive analysis
│
├── technical/                   # Technical design documents
│   ├── INDEX.md                # Master list of technical docs
│   ├── library-decisions.md    # All library choices
│   ├── monorepo-structure.md   # Project organization
│   ├── cpp-coding-standards.md # C++ style guide
│   │
│   ├── vector-graphics/        # Vector rendering system
│   ├── asset-system/           # Asset loading and generation
│   ├── observability/          # Developer tools
│   └── ...                     # Other technical systems
│
└── development-log/             # Implementation history
    ├── README.md               # Index and process documentation
    └── entries/                # Individual dated entries
```

---

## Core Documents

### status.md

**Read this first** to understand where we are in development:
- Current sprint goals and active tasks
- Recent architectural decisions
- Known blockers and issues

**Update after significant work.**

### development-log/

**Detailed historical record** of implementation work. Each entry is a separate file documenting:
- What was built/changed
- Technical decisions made
- Files created/modified

See [development-log/README.md](./development-log/README.md) for the process.

### workflows.md

Common development tasks:
- Recording implementation work
- Adding new libraries
- Creating scenes
- Running tests
- Documentation standards

### setup.md

Development environment setup:
- Prerequisites
- Build system configuration
- IDE setup
- Tool installation

---

## Game Design vs Technical Documentation

| Question | Document Type | Location |
|----------|---------------|----------|
| What does the player experience? | Game Design | `design/` |
| How does this system work from a gameplay perspective? | Game Design | `design/game-systems/` |
| How do we implement this technically? | Technical | `technical/` |
| What library should we use? | Technical | `technical/library-decisions.md` |
| What did we build and when? | Development Log | `development-log/entries/` |

### Examples

| Topic | Design Doc | Technical Doc |
|-------|------------|---------------|
| Colonist needs | `design/game-systems/colonists/needs.md` | ECS components in `technical/cpp-coding-standards.md` |
| Multiplayer | `design/features/multiplayer/README.md` | `technical/multiplayer-architecture.md` |
| Vector graphics | `design/features/vector-graphics/` | `technical/vector-graphics/` |

---

## Documentation Standards

### MVP References

Do NOT create "MVP Scope" sections in individual docs. Instead write:

```markdown
**MVP Status:** See [MVP Scope](./design/mvp-scope.md) — This feature: Phase X
```

### Historical Addendums

When consolidating or superseding documents, add a "Historical Addendum" section at the bottom preserving original content for reference.

### Cross-References

Use relative paths for links within docs/:
```markdown
See [Needs System](./game-systems/colonists/needs.md)
```

---

## Related Files

- [CLAUDE.md](../CLAUDE.md) — Instructions for Claude Code AI assistant
- [README.md](../README.md) — Main project README with build instructions
- [.clang-format](../.clang-format) — Code formatting rules
- [CMakeLists.txt](../CMakeLists.txt) — Build system configuration
