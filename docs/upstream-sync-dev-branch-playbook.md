# Upstream Sync Playbook (Dev Branch + Logging Preserved)

## Goal

Keep `ndr-pacing-lab` close to `upstream/master` so debugging matches what users run, while preserving local logging/tooling needed for investigation.

## Branch Roles

- `ndr-pacing-lab`
  - Primary dev/debug branch.
  - Keeps instrumentation, logging helpers, and workflow docs/scripts.
- `arm64-codeonly-pr` (or future `arm64-upstream-track`)
  - Clean upstream-facing branch.
  - Mirrors what is intended for upstream compatibility and release behavior.

## Default Strategy

Use upstream-first syncing with explicit conflict policy:

1. Pull all upstream commits into `ndr-pacing-lab`.
2. For conflict files tied to local debug instrumentation, keep `ndr-pacing-lab` versions unless there is a specific reason to adopt upstream hunk(s).
3. For unrelated subsystems, prefer upstream.

This keeps debug capability intact while tracking upstream changes everywhere else.

## Sensitive Files (Logging/Instrumentation Surface)

Conflicts in these files should be reviewed with extra care:

- `src/86box.c`
- `src/codegen_new/codegen.c`
- `src/codegen_new/codegen.h`
- `src/codegen_new/codegen_backend_arm64_ops.c`
- `src/codegen_new/codegen_ops.c`
- `src/codegen_new/codegen_ops_3dnow.c`
- `src/codegen_new/codegen_ops_3dnow.h`
- `src/cpu/386_dynarec.c`
- `src/cpu/x86_ops_3dnow.h`

## Recurring Sync Procedure

1. Preflight:
   - `git switch ndr-pacing-lab`
   - `git status --short --branch` (must be clean or intentionally staged)
2. Refresh remotes:
   - `git fetch upstream`
   - `git fetch origin --prune`
3. Inspect upstream delta:
   - `git log --oneline ndr-pacing-lab..upstream/master`
   - Optional sensitive-file check:
     - `git log --oneline ndr-pacing-lab..upstream/master -- <sensitive files...>`
4. Merge upstream:
   - `git merge --no-ff upstream/master`
5. Resolve conflicts:
   - Sensitive logging files: prefer local (`ours`) unless intentionally adopting upstream edits.
   - Other files: prefer upstream (`theirs`) unless they break local workflow.
6. Validate:
   - clean build
   - quick sanity run
   - targeted debug run if dynarec paths changed
7. Commit merge resolution and push:
   - `git push origin ndr-pacing-lab`

## Conflict Handling Guidance

If upstream touched the same instrumentation file:

- Start by keeping local file (`ours`) to preserve debug hooks.
- Then manually re-apply any must-have upstream logic changes into that file.
- Rebuild and run sanity checks immediately after.

Do not blindly accept both sides in instrumentation hotspots.

## Safety Rules

- Do not rebase away debug history on `ndr-pacing-lab` during active investigation cycles.
- Do not delete logging hooks without confirming equivalent observability remains.
- If a sync introduces behavior drift, keep the merge commit and revert narrowly by file/hunk instead of hard-resetting branch history.

## Practical Outcome

This workflow gives:

- Up-to-date baseline with upstream user-facing behavior.
- Retained local observability for rapid regression triage.
- Lower risk of losing debug capability when upstream modifies dynarec code you need to inspect/fix.
