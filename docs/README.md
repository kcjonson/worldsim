# Documentation

Project documentation for World-Sim organized by purpose and audience.

## Quick Start

**New to the project?** Start here:
1. Read the main [README.md](../README.md) for build instructions
2. Check [status.md](./status.md) to see what we're working on
3. Review [setup.md](./setup.md) for development environment setup
4. Consult [workflows.md](./workflows.md) for common tasks

**Working on a feature?**
1. Check [design/INDEX.md](./design/INDEX.md) for game design requirements
2. Check [technical/INDEX.md](./technical/INDEX.md) for implementation details
3. Update [status.md](./status.md) when done

## Directory Structure

```
docs/
├── README.md                    # This file
├── status.md                    # Current project status (START HERE)
├── setup.md                     # Development environment setup
├── workflows.md                 # Common development tasks
│
├── design/                      # Game design documents
│   ├── INDEX.md                # Master list of design docs
│   ├── competitive-analysis.md # Market research
│   │
│   ├── features/               # Feature designs
│   │   ├── vector-assets/
│   │   ├── debug-server/
│   │   ├── multiplayer/
│   │   └── ...
│   │
│   └── requirements/           # Requirements documents (planned)
│       ├── functional.md
│       ├── non-functional.md
│       └── performance.md
│
└── technical/                   # Technical design documents
    ├── INDEX.md                # Master list of technical docs
    ├── monorepo-structure.md   # Project organization
    ├── cpp-coding-standards.md # C++ style guide
    └── ...                     # System-specific design docs
```

## Core Documents

### status.md
**Read this first** to understand where we are in development:
- Current sprint goals
- Completed work
- Active decisions
- Known blockers
- Development log
- use checkboxes
- do not put code or decisions here, this is purely a task list

**Update this after significant work.**

### workflows.md
Common development tasks:
- Adding new libraries
- Creating scenes
- Running tests
- Debugging
- Updating documentation

### setup.md
Development environment setup:
- Prerequisites
- Build system configuration
- IDE setup
- Tool installation

## Technical Documentation

Location: `technical/`

**Purpose**: Describes *how* systems are implemented

**What belongs here**:
- Architecture and system design (client/server, ECS, rendering)
- Implementation details (algorithms, data structures, protocols)
- Tools and infrastructure (debug server, asset pipeline, build system)
- Performance optimization and profiling
- Technical patterns and conventions (logging, memory management, etc.)

**What does NOT belong here** (goes in `design/`):
- Player-facing game features and mechanics
- UI/UX from the player's perspective
- Game systems from a design perspective (not implementation)
- Player-facing requirements

**Index**: See [technical/INDEX.md](./technical/INDEX.md)

**Structure**: Flexible - organize as makes sense. Prefer many short focused docs (1-5 pages) over long comprehensive documents.

**Key documents**:
- `monorepo-structure.md` - Library organization and dependencies
- `cpp-coding-standards.md` - C++ style guide and best practices
- `vector-asset-pipeline.md` - SVG rendering system
- `multiplayer-architecture.md` - Client/server implementation
- `http-debug-server.md` - Development tooling
- Engine patterns (string hashing, logging, memory arenas, etc.)

**When to read**: Before implementing any system or modifying existing code

**When to write**: After making architectural decisions or implementing new systems

**Best Practices**: 
- Code in technical docs should describe or demonstrate HOW something complex should be done, not have actual production code that is meant to be used. There should only be code to explain to the reader something unique or novel to the approach such as algorithms, data structures or best practices. There might also be code in a technical docs that shows multiple ways of doing the same thing for debate. The technical document might show a tree of where the files will be created and in human language what those files are responsible for. The actual codebase will contain the final code of record.
- Should reference and link the design documents where relevant
- Should not link to code in the codebase that was written as a result or output of this document. This document is the starting point for code that is to be generated, and logically can not link to that code since it has not been written. Do not update these documents after the code has been written. 

## Game Design Documents

Location: `design/`

**Purpose**: Describes *what* needs to be built from a **player-facing game design perspective**

**What belongs here**:
- Player experience and game mechanics
- UI/UX from the player's perspective
- Game systems (building, colonists, raids, etc.)
- Gameplay features and content
- Player-facing requirements

**What does NOT belong here** (goes in `technical/`):
- Architecture and system design (client/server, ECS, etc.)
- Implementation details (algorithms, data structures)
- Tools and infrastructure (debug server, asset pipeline)
- Performance optimization
- Technical systems (networking, rendering, etc.)

**Index**: See [design/INDEX.md](./design/INDEX.md)

**Structure**: Flexible - use subdirectories (`features/`, `requirements/`) only when organization requires it. Prefer many short focused docs (1-5 pages) over long comprehensive documents.

**When to read**: When starting a new feature or understanding player-facing requirements

**When to write**: During game design and feature planning

**Note**: Design docs may differ from actual implementation. When implementation diverges, the technical docs describe what was actually built and why.

## Player-Facing vs Technical: Examples

To clarify the distinction:

| Topic | Design Doc (Player-Facing) | Technical Doc (Implementation) |
|-------|---------------------------|-------------------------------|
| **Multiplayer** | "Players can join each other's colonies and collaborate" | Client/server architecture, WebSocket protocol, state sync |
| **Building System** | "Players can construct buildings that affect colonist behavior" | Building entity structure, ECS components, placement validation |
| **World Creation** | "Players customize world parameters and preview the planet" | 3D noise generation algorithms, sphere sampling, OpenGL rendering |
| **UI** | "Main menu with New Game, Load, Settings buttons" | ImGui implementation, scene management, input handling |
| **Debug Tools** | N/A - This is purely technical | HTTP debug server, SSE streaming, metrics collection |
| **Asset Pipeline** | N/A - This is purely technical | SVG parsing, rasterization, texture atlas generation |

## Documentation Workflow

### When You're Lost
1. Check [status.md](./status.md) - Where are we?
2. Check [workflows.md](./workflows.md) - How do I do X?
3. Check [technical/INDEX.md](./technical/INDEX.md) - How is X implemented?
4. Check [design/INDEX.md](./design/INDEX.md) - What are the game design requirements for X?

### When Implementing a Feature
```
1. Read the game design   → design/features/{feature}/
2. Read the tech design   → technical/{system}.md
3. Write the code         → libs/ or apps/
4. Update status          → status.md
```

### When Making Technical Decisions
```
1. Document decision      → technical/{system}.md (new or updated)
2. Add to index          → technical/INDEX.md
3. Update status         → status.md
4. Note in CLAUDE.md     → If high-level structural change
```

### When Completing Work
```
1. Update implementation status → technical/{system}.md
2. Add code references         → technical/{system}.md
3. Update status log           → status.md
4. Update project README       → If user-facing change
```

## Document Types

### Game Design Documents
- **Audience**: Anyone who needs to understand what the game should be
- **Content**: Player experience, game mechanics, feature requirements, acceptance criteria
- **Format**: Markdown with mockups, flows, and diagrams
- **Location**: `design/features/`

### Technical Design Documents
- **Audience**: Developers implementing the system
- **Content**: Architecture, patterns, implementation details, code examples
- **Format**: Markdown with code snippets and diagrams
- **Location**: `technical/`

### Requirements Documents
- **Audience**: Project stakeholders, designers, and developers
- **Content**: Functional, non-functional, and performance requirements
- **Format**: Markdown with structured lists and criteria
- **Location**: `design/requirements/`

### Status and Workflow Documents
- **Audience**: All contributors
- **Content**: Current state, common tasks, setup instructions
- **Format**: Markdown with checklists and commands
- **Location**: `docs/` root

## Documentation Standards

### General Guidelines
- Use GitHub-flavored Markdown
- Include code references as `file.cpp:123` for easy navigation
- Link between documents liberally
- Keep INDEX files up to date
- Use clear, descriptive headings
- Include examples where helpful

### File Naming
- Lowercase with hyphens: `system-name.md`
- README.md for directory overviews
- INDEX.md for master lists

### Structure
Each game design doc should include:
- Feature overview
- Player experience goals
- Game mechanics description
- UI/UX requirements
- Acceptance criteria
- Technical considerations (if any)

Each technical design doc should include:
- Overview / Purpose
- Architecture / Design
- Implementation details
- Code examples
- Related documentation links

## Related Files

- [CLAUDE.md](../CLAUDE.md) - Instructions for Claude Code AI assistant
- [README.md](../README.md) - Main project README with build instructions
- [.clang-format](../.clang-format) - Code formatting rules
- [CMakeLists.txt](../CMakeLists.txt) - Build system configuration
