---
id: VQ-1
title: Prepare Hidpress firmware for open-source publication
status: backlog
priority: high
depends_on: []
run_id: null
canceled: false
worker_model: strong
worker_effort: high
worker_sizing_rationale: "The Vial-QMK fork is large and submodule-heavy; release readiness requires tracing Hidpress-specific changes, upstream obligations, and reproducible builds without disturbing unrelated firmware."
worker_provider_notes: "none"
---

## Description

Audit and prepare the Hidpress portions of the Vial-QMK fork for public release
without publishing them yet. Focus on `keyboards/hidpress/` plus any supporting
changes elsewhere in the fork. Make the relationship to upstream QMK/Vial and
the supported hardware revisions explicit.

## Acceptance criteria

- [ ] Hidpress-specific files and any required changes outside `keyboards/hidpress/` are inventoried and separated from unrelated upstream content.
- [ ] The fork's upstream base, divergence, update strategy, and applicable QMK/Vial licensing obligations are documented.
- [ ] Bipedal Southpaw, Bipedal65, and Skeletal Ambi each state supported revisions, build targets, flashing path, and Vial compatibility.
- [ ] A clean checkout initialises required submodules and builds each documented Hidpress firmware target with the documented commands.
- [ ] Vial identifiers, keymaps, generated files, release binaries, and source files are reviewed for consistency and accidental duplication.
- [ ] No credentials, private data, machine-specific paths, or unrelated local artifacts are included.
- [ ] Publication and any upstream pull request remain separate deliberate actions.
