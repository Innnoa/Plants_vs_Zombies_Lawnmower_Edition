---
name: agtx-execute
description: Execute an approved implementation plan. Implement the changes, then write a summary to .agtx/execute.md and stop.
---

# Execution Phase

You are in the **execution phase** of an agtx-managed task.

Treat this task as `AGTX_CONTEXT=true`:
- `.agtx/plan.md` is the approved scope unless the user explicitly changes it
- `.agtx/*` files are the primary task state for this task
- If the project also has `AI_GUIDE.md`, `CURRENT_TASK.md`, or `DECISIONS.md`, read only the minimum long-term context that is directly relevant
- Do not use subagents unless the user explicitly requires delegation
- Do not silently deviate from the approved plan; if the plan is wrong or incomplete, document that clearly

## Input

- **Task description** — provided inline with this command (when entering directly from Backlog)
- **`.agtx/plan.md`** — approved implementation plan (when planning was completed first)

## Instructions

1. If `.agtx/plan.md` exists, read it for the approved plan
2. Read and understand the task description
3. Implement only the approved scope, unless the user explicitly expands it
4. Run relevant tests or checks to verify your changes
5. Fix any issues found during testing
6. If tests cannot run, record exactly why and what risk remains

## Output

When implementation is complete, write a summary to `.agtx/execute.md` with these sections:

## Changes
What files were modified/created and what was changed in each.

## Testing
How you verified the changes — tests run, results, manual checks.

## Deviations
Any differences from `.agtx/plan.md`, or `None`.

## Remaining Risks
Residual risks, unverified paths, or follow-up work.

## CRITICAL: Stop After Writing

After writing `.agtx/execute.md`:
- Do NOT start new work beyond the plan
- Say: "Implementation complete. Summary written to `.agtx/execute.md`."
- Wait for further instructions
