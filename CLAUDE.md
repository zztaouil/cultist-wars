# Cultist Wars continuation

Arena trial update: user explicitly requested submission of the larger policy.
Submitted **41711350**; archived in `submissions/41711350.c`. Initial ranking
0%, pending. Earlier no-submission statements below are historical.

Latest revision (2026-11-14): read `validation/nn-large-20261114/REVIEW.md` and
`validation/nn-large-20261114/RESULTS.md` for reconciled experiment evidence.
The handoff and historical bullets below predate current submission 41711135.
A 24,833-parameter policy was trained and tested: boss 2W/4D/2L versus baseline
3W/0D/5L in eight matched cases; local 43.75%. No proven BOSS 3 superiority;
no promotion or submission. Preserve the isolated candidate and negative evidence.

Read `HANDOFF.md` first. Then `bot.c`, `README.md`, `validation/strength/README.md`.

- Goal: improve equal-time playing strength toward Legend; depth/node count alone not success.
- Preserve existing uncommitted work and experimental artifacts. Git HEAD lacks most completed work.
- Production: standalone C11 `bot.c`. Keep user-selected 49 ms budget.
- Last submitted source: `submissions/41709358.c`. Current `bot.c` adds local-only lazy Q evaluation; strength gain unproven. No new submission requested by this handoff.
- Existing private Gold-boss test authorization recorded. Read CodinGame skill/API instructions before external work; respect live limits. Never expose credentials.
- Keep experiments isolated, compare against frozen baseline, retain negative results.
- User communication preference: caveman skill, ultra intensity. Local skill: `/Users/hexagone/.agents/skills/caveman/SKILL.md`. If unavailable: terse English, accurate technical terms, no filler; expand when needed for clarity.
- Standard checks: `make test`, `make sanitize`, `python3 tests/test_differential.py`.
- Keep commits focused. Commit-message hooks and GitHub validation were removed.
