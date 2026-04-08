---
name: agtx-research
description: Explore the codebase to understand a task before planning. Write findings to .agtx/research.md and stop. This is a read-only exploration — do not modify any files.
---

# Research Phase

You are in the **research phase** of an agtx-managed task. This is a read-only exploration.

Treat this task as `AGTX_CONTEXT=true`:
- The task's primary state lives in `.agtx/*`
- If the project also has `AI_GUIDE.md`, `CURRENT_TASK.md`, or `DECISIONS.md`, read only the minimum long-term context that is directly relevant
- Base conclusions on code, configuration, command output, or documentation evidence; do not guess

## Input

- **Task description** — provided inline with this command
- **Existing `.agtx/*` files** — read any existing task artifacts if they are present and relevant

## Instructions

1. Read and understand the task description
2. Read any existing `.agtx/*` task artifacts that add relevant context
3. Explore the codebase to find relevant files, patterns, and architecture
4. Identify dependencies, related code, constraints, and potential complexity
5. Assess feasibility and estimate scope
6. Capture open questions only when they are grounded in missing facts, not speculation

## Output

Write your findings to `.agtx/research.md`. Include:

## Relevant Files
Key files and their roles — what exists, what needs changing.

## Architecture
How the relevant parts of the codebase fit together.

## Complexity
Assessment of scope — simple change, moderate refactor, or major undertaking.

## Constraints
Important workflow, product, or technical constraints that planning must respect.

## Open Questions
Things that need clarification before planning can begin.

## Evidence
Concrete commands, files, or observations that support your conclusions.

## CRITICAL: Do Not Modify Code

This is a **read-only** exploration:
- Do NOT modify any source files
- Do NOT create branches or worktrees
- Do NOT use subagents unless the user explicitly requires delegation
- Do NOT start planning or implementing
- Say: "Research complete. Findings written to .agtx/research.md."
- Wait for further instructions
