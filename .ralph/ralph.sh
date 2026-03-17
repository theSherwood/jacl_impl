#!/bin/bash
# Ralph Wiggum - Long-running AI agent loop
# Usage: ./ralph.sh [max_iterations]

set -eo pipefail

# Validate MAX_ITERATIONS
MAX_ITERATIONS=${1:-10}
if ! [[ "$MAX_ITERATIONS" =~ ^[1-9][0-9]*$ ]]; then
  echo "Error: max_iterations must be a positive integer (got: '$MAX_ITERATIONS')" >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRD_FILE="$SCRIPT_DIR/prd.json"
PROGRESS_FILE="$SCRIPT_DIR/progress.txt"
FEEDBACK_FILE="$SCRIPT_DIR/feedback.txt"
ARCHIVE_DIR="$SCRIPT_DIR/archive"
LAST_BRANCH_FILE="$SCRIPT_DIR/.last-branch"
LAST_PRD_FILE="$SCRIPT_DIR/.last-prd.json"

# Preflight checks
if ! command -v claude &>/dev/null; then
  echo "Error: 'claude' not found in PATH" >&2
  exit 1
fi
if ! command -v jq &>/dev/null; then
  echo "Error: 'jq' not found in PATH" >&2
  exit 1
fi
if [ ! -f "$PRD_FILE" ]; then
  echo "Error: prd.json not found at $PRD_FILE" >&2
  exit 1
fi

# Temp file for capturing claude output; cleaned up on any exit
TMPFILE=$(mktemp)
trap 'rm -f "$TMPFILE"' EXIT INT TERM

# Archive previous run if branch changed
CURRENT_BRANCH=$(jq -r '.branchName // empty' "$PRD_FILE")
if [ -f "$LAST_BRANCH_FILE" ] && [ -n "$CURRENT_BRANCH" ]; then
  LAST_BRANCH=$(cat "$LAST_BRANCH_FILE")

  if [ -n "$LAST_BRANCH" ] && [ "$CURRENT_BRANCH" != "$LAST_BRANCH" ]; then
    DATE=$(date +%Y-%m-%d)
    FOLDER_NAME=$(echo "$LAST_BRANCH" | sed 's|^ralph/||')
    ARCHIVE_FOLDER="$ARCHIVE_DIR/$DATE-$FOLDER_NAME"

    echo "Archiving previous run: $LAST_BRANCH"
    mkdir -p "$ARCHIVE_FOLDER"
    # Use .last-prd.json if available — preserves the old run's PRD even after prd.json is replaced
    if [ -f "$LAST_PRD_FILE" ]; then
      cp "$LAST_PRD_FILE" "$ARCHIVE_FOLDER/prd.json"
    else
      cp "$PRD_FILE" "$ARCHIVE_FOLDER/prd.json"
    fi
    [ -f "$PROGRESS_FILE" ] && cp "$PROGRESS_FILE" "$ARCHIVE_FOLDER/"
    [ -f "$FEEDBACK_FILE" ] && cp "$FEEDBACK_FILE" "$ARCHIVE_FOLDER/"
    echo "   Archived to: $ARCHIVE_FOLDER"

    # Update .last-branch BEFORE resetting progress — avoids re-archiving with empty
    # progress.txt if the script is killed between the two writes
    echo "$CURRENT_BRANCH" > "$LAST_BRANCH_FILE"

    # Reset state files for new run
    printf "# Ralph Progress Log\nStarted: %s\n---\n" "$(date)" > "$PROGRESS_FILE"
    rm -f "$FEEDBACK_FILE"
  fi
fi

# Always ensure .last-branch reflects the current prd.json
if [ -n "$CURRENT_BRANCH" ]; then
  echo "$CURRENT_BRANCH" > "$LAST_BRANCH_FILE"
fi

# Initialize progress file if it doesn't exist
if [ ! -f "$PROGRESS_FILE" ]; then
  printf "# Ralph Progress Log\nStarted: %s\n---\n" "$(date)" > "$PROGRESS_FILE"
fi

echo "Starting Ralph - Max iterations: $MAX_ITERATIONS"

for i in $(seq 1 "$MAX_ITERATIONS"); do
  echo ""
  echo "═══════════════════════════════════════════════════════"
  echo "  Ralph Iteration $i of $MAX_ITERATIONS"
  echo "═══════════════════════════════════════════════════════"

  # Run agent — stream all output (including tool calls and thinking) to stdout via tee,
  # and capture to temp file for completion signal detection.
  # --verbose: shows tool calls, tool results, and thinking blocks
  CLAUDE_EXIT=0
  claude --dangerously-skip-permissions --effort high --model claude-opus-4-6 < "$SCRIPT_DIR/prompt.md" 2>&1 \
    | tee "$TMPFILE" \
    || CLAUDE_EXIT=$?

  if [ "$CLAUDE_EXIT" -ne 0 ]; then
    echo ""
    echo "Warning: claude exited with error (exit $CLAUDE_EXIT) on iteration $i" >&2
  fi

  OUTPUT=$(cat "$TMPFILE")

  # Check for completion signal
  if echo "$OUTPUT" | grep -q "<promise>COMPLETE</promise>"; then
    echo ""
    echo "Ralph completed all tasks!"
    echo "Completed at iteration $i of $MAX_ITERATIONS"
    cp "$PRD_FILE" "$LAST_PRD_FILE"
    exit 0
  fi

  # Check for needs-input signal
  if echo "$OUTPUT" | grep -q "<promise>NEEDS_INPUT</promise>"; then
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  Ralph needs your input (question above)"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    printf "Your response: "
    read -r USER_RESPONSE < /dev/tty
    {
      echo "## Feedback [$(date)]"
      echo "$USER_RESPONSE"
      echo "---"
    } >> "$FEEDBACK_FILE"
    echo "Feedback recorded. Resuming..."
    continue
  fi

  # Snapshot prd.json so future archiving gets this run's PRD, not the next run's
  cp "$PRD_FILE" "$LAST_PRD_FILE" 2>/dev/null || true

  echo "Iteration $i complete. Continuing..."
  sleep 2
done

cp "$PRD_FILE" "$LAST_PRD_FILE" 2>/dev/null || true

echo ""
echo "Ralph reached max iterations ($MAX_ITERATIONS) without completing all tasks."
echo "Check $PROGRESS_FILE for status."
exit 1
