# Agent memory

This directory keeps project-local working context that should survive across implementation sessions. It records decisions, evidence, hypotheses, and open questions; it is not a substitute for source code or a task-specific implementation plan.

## Areas

- [Audibilization](audibilization/README.md): reverse-engineering notes and the proposed deterministic sorting-audio playback architecture.
- [Sorting roster progress](sorting-roster-progress.md): pinned ArrayV inventory, the current independent implementation batch, and the uncompleted coverage/UI work.

## Maintenance rules

- Label direct observations separately from interpretations and proposals.
- Keep source paths, timestamps, settings, and calculations with claims that may need to be checked again.
- Update the relevant notes when new evidence changes confidence or supersedes a hypothesis.
- Do not treat an unverified hypothesis as an implementation requirement.

Current status: the historical first refactoring wave is recorded in [the refactoring roadmap](refactoring-roadmap.md). The app now has a separate sound engine; the larger pinned-roster showcase is in progress. See [sorting roster progress](sorting-roster-progress.md) for verified scope and remaining work.
