# Upstream Sync Workflow

Use this workflow instead of GitHub's "Sync fork" button. It keeps upstream code moving into the fork without rewriting `master` or blindly replacing fork-specific docs.

## Recommended flow

From a clean working tree:

```bash
./scripts/sync-upstream.sh
```

That script will:

- fetch `upstream`
- fast-forward local `master` from `origin/master`
- create a dated branch like `sync/upstream-2026-03-10`
- merge `upstream/master` into that sync branch
- restore known fork-owned files from the pre-merge `master` snapshot, even if Git fast-forwards cleanly into upstream history

## Fork-owned files currently restored from the fork branch

- `.gitignore`
- `README.md`
- `appimage/`
- `docs/upstream-sync-workflow.md`
- fork-specific scripts in `scripts/`
- `voodoo-arm64-port/`

If more fork-specific docs or branding files should always stay local, add them to `keep_ours_paths` in [`scripts/sync-upstream.sh`](/Users/anthony/projects/code/86Box-voodoo-arm64/scripts/sync-upstream.sh).

## After the script runs

The sync branch will usually need one follow-up commit that reapplies fork-owned files:

```bash
git status
git commit -m "Restore fork-owned files after upstream sync"
git log --oneline --decorate --graph -20
# run build/tests you care about
git checkout master
git merge --ff-only sync/upstream-YYYY-MM-DD
git push origin master
```

If there are still conflicts after the script auto-resolves known paths:

```bash
git diff --name-only --diff-filter=U
# resolve remaining conflicts
git add <resolved-files>
git commit
```

Then fast-forward `master` and push as above.
