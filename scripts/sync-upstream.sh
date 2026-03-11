#!/usr/bin/env bash

set -euo pipefail

branch_date="$(date +%Y-%m-%d)"
sync_branch="sync/upstream-${branch_date}"
base_branch="${1:-master}"
base_commit=""

keep_ours_paths=(
  ".gitignore"
  "README.md"
  "appimage"
  "docs/upstream-sync-workflow.md"
  "scripts/README-jit-analyzer.md"
  "scripts/analyze-jit-log-win32.c"
  "scripts/analyze-jit-log.c"
  "scripts/analyze-jit-log.py"
  "scripts/build-and-sign.sh"
  "scripts/clean-build-and-sign.sh"
  "scripts/setup-and-build.sh"
  "scripts/sync-upstream.sh"
  "scripts/test-with-vm.sh"
  "voodoo-arm64-port"
)

restore_fork_owned_paths() {
  local restored_any=0
  local path

  echo
  echo "Reapplying fork-owned paths from ${base_commit}..."

  for path in "${keep_ours_paths[@]}"; do
    if git cat-file -e "${base_commit}:${path}" 2>/dev/null; then
      git restore --source="${base_commit}" --staged --worktree -- "${path}"
      echo "Restored ${path}"
      restored_any=1
    fi
  done

  if [[ ${restored_any} -eq 0 ]]; then
    echo "No fork-owned paths needed restoration."
  fi
}

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "error: this script must be run inside a git work tree" >&2
  exit 1
fi

if [[ -n "$(git status --porcelain)" ]]; then
  echo "error: working tree is not clean; commit or stash changes first" >&2
  exit 1
fi

if ! git show-ref --verify --quiet "refs/heads/${base_branch}"; then
  echo "error: local branch '${base_branch}' does not exist" >&2
  exit 1
fi

current_branch="$(git branch --show-current)"

echo "Fetching upstream..."
git fetch upstream

echo "Refreshing ${base_branch} from origin/${base_branch}..."
git checkout "${base_branch}"
git pull --ff-only origin "${base_branch}"
base_commit="$(git rev-parse "${base_branch}")"

if git show-ref --verify --quiet "refs/heads/${sync_branch}"; then
  echo "error: branch '${sync_branch}' already exists" >&2
  exit 1
fi

echo "Creating ${sync_branch} from ${base_branch}..."
git switch -c "${sync_branch}"

echo "Merging upstream/master into ${sync_branch}..."
set +e
git merge upstream/master
merge_status=$?
set -e

if [[ ${merge_status} -ne 0 ]]; then
  echo
  echo "Merge reported conflicts. Auto-resolving fork-owned docs with --ours where possible..."

  unresolved=0
  for path in "${keep_ours_paths[@]}"; do
    if git ls-files -u -- "${path}" | grep -q .; then
      echo "Keeping local version of ${path}"
      git checkout --ours -- "${path}"
      git add "${path}"
    fi
  done

  if git diff --name-only --diff-filter=U | grep -q .; then
    unresolved=1
  fi

  if [[ ${unresolved} -ne 0 ]]; then
    echo
    echo "Some conflicts still need manual resolution:"
    git diff --name-only --diff-filter=U
    echo
    echo "When you are done, run:"
    echo "  git add <resolved-files>"
    echo "  git commit"
    exit 1
  fi

  restore_fork_owned_paths

  echo
  echo "Only known conflicts remain resolved, and fork-owned paths were reapplied."
  echo "Finish the merge with:"
  echo "  git status"
  echo "  git commit"
  exit 0
fi

restore_fork_owned_paths

echo
echo "Merge completed on ${sync_branch}."
echo "Next steps:"
echo "  1. Review restored fork-owned paths: git status"
echo "  2. Commit the reapplied fork-owned paths:"
echo "     git commit -m \"Restore fork-owned files after upstream sync\""
echo "  3. Review history: git log --oneline --decorate --graph -20"
echo "  4. Test/build as needed"
echo "  5. Fast-forward master after review:"
echo "     git checkout ${base_branch}"
echo "     git merge --ff-only ${sync_branch}"
echo "  6. Push when ready:"
echo "     git push origin ${base_branch}"

if [[ -n "${current_branch}" && "${current_branch}" != "${sync_branch}" ]]; then
  echo
  echo "Previous branch was ${current_branch}."
fi
