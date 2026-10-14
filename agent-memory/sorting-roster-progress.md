# Sorting roster and semantic-audio progress

## Scope baseline

- Upstream source: [ArrayV](https://github.com/Gaming32/ArrayV), pinned to commit `74f6ada530f0ed0ae38fe95db40ebf971a19167c`.
- The checked-in [coverage manifest](arrayv-catalog-coverage.csv) lists 208 selectable source classes. The family assignments are an initial grouping aid; they are not evidence that an entry has been implemented.
- All 208 manifest rows currently remain `not_implemented`. No upstream source is marked covered until its behavior has been compared with a documented app variant and validated.

## Implemented in the current working tree

- The catalog has 16 independent implementations: Insertion, Bubble, Selection, Shell, Merge, Quick, Heap, Counting, Radix, Gnome, Cocktail Shaker, Comb, Odd-Even Transposition, Pancake, Stooge, and bounded seeded Bogo Sort.
- Related implementations are grouped into stable families: Insertion and Shell methods; adjacent/gap exchange sorts (Bubble, Cocktail Shaker, Gnome, Comb, Odd-Even Transposition); Selection and Pancake methods; and Counting/Radix distribution sorts. Merge, partition, heap, and the two novelty approaches keep distinct families. Each concrete implementation has its own stable variant ID.
- Stable `FamilyId`/`VariantId` values now travel through settings, app actions, and trace-build requests. The selector labels show family and implementation together. Older v4 per-algorithm IDs still load and normalize into grouped IDs; a searchable family/variant browser is not implemented.
- Traces support deterministic random, sorted, reversed, nearly sorted, few-unique, and organ-pipe inputs across normalized floats, nonnegative integer keys, signed integer keys, and integer permutations, with compatibility checks.
- Trace state supports probes, reversible discards, group markers, named auxiliary workspaces, counter workspaces, and an operation budget that returns a reversible partial trace.
- Merge Sort now has a saved presentation choice: animated writes or comparison-first atomic range writes. The atomic operation stores before/after slot ranges so it can be replayed and rewound exactly.
- The audio bridge coalesces per-frame Probe, Movement, Distribution, and Discard summaries into the existing latest-state mailbox. The synthesizer applies them as configurable quieter accents with event-category routing and gains; comparisons retain their existing path.
- Version-4 settings include the current family/variant, input domain/pattern, Merge writeback mode, and event-category audio routing/gains. Older v1–v3 migration remains covered by the framework tests.

## Not complete

- The current 16-entry app catalog is only a first batch; the 208-entry coverage manifest is still wholly uncovered.
- The UI still uses an algorithm combo box. It does not provide searchable families, variant details, filters, compatibility explanations per catalog entry, or configuration controls for the full roster.
- Family-specific audio routing/gain presets are not implemented; current audio controls are global by event category.
- Only existing algorithms have real sorting strategies. Network phases, the remaining ArrayV families, additional radix/selection/merge variants, novelty methods, incomplete methods, intentionally item-discarding methods, and per-variant safe limits still need independent implementations and tests.
- The current Windows Computer Use tool exposed browser controls only, with no native-window API, so this turn could not visually inspect the SDL window. The x64 Debug application build succeeded.

## Latest verification

- Full solution builds passed for Win32 Debug, Win32 Release, x64 Debug, and x64 Release.
- `SortingLogicTests`: 104,406 checks, 0 failures after family grouping and stable variant-ID validation.
- `AppFrameworkTests`: passed, including settings round-trip and indexed semantic-audio actions.
- `SoundEngineTests`: 941 assertions passed, including routed accents and accent release.
- Full solution builds passed for Win32 Debug, Win32 Release, x64 Debug, and x64 Release.
- `git diff --check`: passed; Git reported only its usual working-tree line-ending conversion notices.
- All 208 manifest entries, the searchable family browser, family-specific audio configuration, and visual review remain outstanding for this wave.
