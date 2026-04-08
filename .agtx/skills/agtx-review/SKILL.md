---
name: agtx-review
description: Self-review completed work. Check for correctness, edge cases, and code quality. Write review to .agtx/review.md and stop.
---

# Review Phase

You are in the **review phase** of an agtx-managed task.

Treat this task as `AGTX_CONTEXT=true`:
- `.agtx/*` files are the primary task state for this task
- Review should focus on findings first: bugs, regressions, missing validation, and residual risks
- If the project also has `AI_GUIDE.md` or `DECISIONS.md`, read only the minimum long-term context that is directly relevant
- Do not use subagents unless the user explicitly requires delegation

## Instructions

1. Read `.agtx/plan.md` and `.agtx/execute.md` if they exist
2. Review all changes made during execution (use git diff)
3. Check for:
   - Correctness and edge cases
   - Error handling
   - Code style consistency with the existing codebase
   - Test coverage
   - Security issues (injection, XSS, etc.)
   - Deviations from the approved plan
4. Fix any issues you find
5. Re-run the most relevant validation after fixes when practical

## Output

Write your review to `.agtx/review.md` with these sections:

## Findings
List findings first, ordered by severity. If there are no material findings, state that explicitly.

## Fixes Made
What you fixed during review, or `None`.

## Residual Risks
Anything still unverified, questionable, or intentionally deferred.

## Status
Either `READY` (good to merge) or `NEEDS_WORK` (with explanation of remaining issues).

## CRITICAL: Stop After Writing

After writing `.agtx/review.md`:
- Say: "Review written to `.agtx/review.md`."
- Wait for further instructions
