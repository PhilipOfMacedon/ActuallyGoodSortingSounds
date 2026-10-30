# Agent memory

This directory keeps project-local working context that should survive across implementation sessions. It records decisions, evidence, hypotheses, and open questions; it is not a substitute for source code or a task-specific implementation plan.

## Areas

- [Audibilization](audibilization/README.md): reverse-engineering notes and the proposed deterministic sorting-audio playback architecture.
- [Sorting roster progress](sorting-roster-progress.md): pinned ArrayV inventory, implementation evidence, and verification history.
- [Structural architecture audit](structural-architecture-audit.md): current ownership and coupling review, concrete duplication, runtime optimization candidates, and a behavior-preserving refactoring sequence.
- [Structural refactor progress](structural-refactor-progress.md): implementation slices, verification results, and the next open boundary from that audit.
- [GUI scrolling](gui-scroll-refactor.md): shared viewport geometry, current adoption, and live-verification notes.
- [Interface ideas](interface-ideas.md): user-approved visual directions that remain proposals until implementation is requested.
- [Demo UI repairs](demo-ui-repairs.md): transport handoff, shared component/preset repairs, validation, and the pending editor design concept.

## Local owner guidance

The coding and Git workflow references supplied by the owner are saved verbatim in `.local-guidance/CODING_PRACTICES.md` and `.local-guidance/GIT_WORKFLOW.md` at the repository root. That directory is intentionally Git-ignored. Root `AGENTS.md` makes these references discoverable to future coding sessions; missing copies on another machine are expected. The sibling workflow resolves the coding reference's stale machine-specific workflow path.

## Maintenance rules

- Label direct observations separately from interpretations and proposals.
- Keep source paths, timestamps, settings, and calculations with claims that may need to be checked again.
- Update the relevant notes when new evidence changes confidence or supersedes a hypothesis.
- Do not treat an unverified hypothesis as an implementation requirement.

Current status: the historical first refactoring wave is recorded in [the refactoring roadmap](refactoring-roadmap.md). The app now has separate sorting and sound libraries, and the pinned coverage manifest records all 208 upstream rows as `implemented_and_tested`. That recorded status does not rule out undiscovered correctness bugs. The next proposed wave addresses structural debt; see [the architecture audit](structural-architecture-audit.md). Roster implementation evidence remains in [sorting roster progress](sorting-roster-progress.md).
