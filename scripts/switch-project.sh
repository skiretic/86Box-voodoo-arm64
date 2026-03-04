#!/bin/bash
# Switch between Voodoo, NV3, and VideoCommon project modes for Claude Code.
# Usage: ./scripts/switch-project.sh [voodoo|nv3|videocommon|cpu|status]
#
# Swaps:
#   CLAUDE.md              <-- CLAUDE.{voodoo,nv3,videocommon}.md
#   .claude/settings.local.json  <-- .claude/settings.local.{voodoo,nv3,videocommon}.json
#   .claude/agents/        <-- .claude/agents-{voodoo,nv3,videocommon}/
#
# Restart Claude Code after switching.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLAUDE_DIR="$REPO_ROOT/.claude"

current_mode() {
    if [ -f "$REPO_ROOT/CLAUDE.md" ]; then
        if grep -q "NV3 Override" "$REPO_ROOT/CLAUDE.md" 2>/dev/null; then
            echo "nv3"
        elif grep -q "VideoCommon Override" "$REPO_ROOT/CLAUDE.md" 2>/dev/null; then
            echo "videocommon"
        elif grep -q "CPU Dynarec Override" "$REPO_ROOT/CLAUDE.md" 2>/dev/null; then
            echo "cpu"
        elif grep -q "Voodoo Override" "$REPO_ROOT/CLAUDE.md" 2>/dev/null; then
            echo "voodoo"
        else
            echo "unknown"
        fi
    else
        echo "none"
    fi
}

switch_to() {
    local mode="$1"

    # Validate source files exist
    if [ ! -f "$CLAUDE_DIR/CLAUDE.${mode}.md" ]; then
        echo "ERROR: $CLAUDE_DIR/CLAUDE.${mode}.md not found"
        exit 1
    fi
    if [ ! -f "$CLAUDE_DIR/settings.local.${mode}.json" ]; then
        echo "ERROR: $CLAUDE_DIR/settings.local.${mode}.json not found"
        exit 1
    fi
    if [ ! -d "$CLAUDE_DIR/agents-${mode}" ]; then
        echo "ERROR: $CLAUDE_DIR/agents-${mode}/ not found"
        exit 1
    fi

    # Copy CLAUDE.md
    cp "$CLAUDE_DIR/CLAUDE.${mode}.md" "$REPO_ROOT/CLAUDE.md"
    echo "  CLAUDE.md <- CLAUDE.${mode}.md"

    # Copy settings
    cp "$CLAUDE_DIR/settings.local.${mode}.json" "$CLAUDE_DIR/settings.local.json"
    echo "  settings.local.json <- settings.local.${mode}.json"

    # Swap agents
    rm -rf "$CLAUDE_DIR/agents/"
    mkdir -p "$CLAUDE_DIR/agents"
    cp "$CLAUDE_DIR/agents-${mode}/"*.md "$CLAUDE_DIR/agents/"
    echo "  agents/ <- agents-${mode}/"

    echo ""
    echo "Switched to: $mode"
    echo "Restart Claude Code to pick up the changes."
}

case "${1:-status}" in
    voodoo)
        echo "Switching to Voodoo mode..."
        switch_to voodoo
        ;;
    nv3)
        echo "Switching to NV3 mode..."
        switch_to nv3
        ;;
    videocommon|vc)
        echo "Switching to VideoCommon mode..."
        switch_to videocommon
        ;;
    cpu|dynarec)
        echo "Switching to CPU Dynarec mode..."
        switch_to cpu
        ;;
    status)
        echo "Current mode: $(current_mode)"
        echo ""
        echo "Active agents:"
        ls "$CLAUDE_DIR/agents/" 2>/dev/null || echo "  (none)"
        ;;
    *)
        echo "Usage: $0 [voodoo|nv3|videocommon|cpu|status]"
        exit 1
        ;;
esac
