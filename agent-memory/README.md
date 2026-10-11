# Agent memory

This directory keeps project-local working context that should survive across implementation sessions. It records decisions, evidence, hypotheses, and open questions; it is not a substitute for source code or a task-specific implementation plan.

## Areas

- [Audibilization](audibilization/README.md): reverse-engineering notes and the proposed deterministic sorting-audio playback architecture.

## Maintenance rules

- Label direct observations separately from interpretations and proposals.
- Keep source paths, timestamps, settings, and calculations with claims that may need to be checked again.
- Update the relevant notes when new evidence changes confidence or supersedes a hypothesis.
- Do not treat an unverified hypothesis as an implementation requirement.

Current status: conceptual research and architecture discussion only. No audibilization code has been added. The user plans to return for an implementation plan after cleaning up other threads.
