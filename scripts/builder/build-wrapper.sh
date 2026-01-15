#!/bin/sh
################################################################################
# Build Wrapper for ryu_ldn_nx Components
################################################################################
#
# PURPOSE:
#   Orchestrates multi-component builds with file-based locking to prevent
#   concurrent builds of interdependent components (e.g., sysmodule waiting
#   for libstratosphere). Captures build logs per component and delegates 
#   actual compilation to Makefiles.
#
# USAGE:
#   ./build-wrapper.sh <component> <working-dir> [--lock <lockfile>]... [make-args]...
#
#   Examples:
#     # Simple build without locks
#     ./build-wrapper.sh overlay . clean
#
#     # Build with dependency lock
#     ./build-wrapper.sh libstratosphere . --lock .lock-libstro nx_release
#
#     # Build with multiple locks (both dependency and own build lock)
#     ./build-wrapper.sh build . --lock .lock-libstro --lock .lock-build clean all
#
# FEATURES:
#   - Multi-lock support: Acquire and hold multiple lock files simultaneously
#   - Automatic lock cleanup: Locks are always removed on success/failure/interrupt
#   - Build logging: All output captured to build-logs/<component>.log
#   - Wait mechanism: Blocks if locks are already held by concurrent builds
#   - Exit code preservation: Returns the make process exit code
#
# LOCK MECHANISM:
#   1. If --lock arguments provided, acquire each lock sequentially
#   2. Hold locks exclusively until build completes
#   3. On exit (any reason), cleanup removes all lock files
#   4. Other builds waiting on these locks are unblocked
#
# LOG FILES:
#   Location: /workspace/build-logs/<component>.log
#   Contains: All stdout and stderr from make, plus build wrapper messages
#
# EXIT CODES:
#   0   = Build successful
#   1   = Lock acquisition failed or make returned error
#   130 = Interrupted by signal (SIGINT, SIGTERM)
#
################################################################################

set -e  # Exit immediately on any error

# ============================================================================
# INITIALIZATION
# ============================================================================

LOCK_FILES=""                           # Space-separated list of lock files to acquire
COMPONENT="${1:-unknown}"               # Build component name (used for logging)
LOG_DIR="/workspace/build-logs"         # Directory for build logs (mounted from host)
LOG_FILE="$LOG_DIR/$COMPONENT.log"      # Log file for this component's build output

# Create logs directory if it doesn't exist
mkdir -p "$LOG_DIR"

# Clear previous log file (start fresh for each build)
: > "$LOG_FILE"

# ============================================================================
# HELPER FUNCTIONS
# ============================================================================

# log_message: Print to both console and log file
# Usage: log_message "Message text"
# Output: Printed to stdout AND appended to $LOG_FILE
log_message() {
    echo "$@" | tee -a "$LOG_FILE"
}

# do_build: Execute make with captured output
# Captures both stdout (1) and stderr (2) to log file
# Returns: Exit code from make process
do_build() {
    make $MAKE_ARGS 2>&1 | tee -a "$LOG_FILE"
}

# ============================================================================
# ARGUMENT PARSING
# ============================================================================
# Skip the component name (arg 1), parse remaining arguments
# Format: [--lock <file> | --lock=<file>]* [make-args]*
#
# Strategy:
#   - Collect all --lock arguments into LOCK_FILES variable
#   - Pass remaining arguments to MAKE_ARGS (targets, variables, flags)
#
# Examples:
#   Input:  ./script.sh build . --lock lock1 --lock lock2 clean all DEBUG=1
#   LOCK_FILES = "lock1 lock2"
#   MAKE_ARGS = "clean all DEBUG=1"

shift 1
MAKE_ARGS=""
while [ $# -gt 0 ]; do
    case "$1" in
        --lock)
            # Format: --lock <file> (with space separator)
            LOCK_FILES="$LOCK_FILES $2"
            shift 2
            ;;
        --lock=*)
            # Format: --lock=<file> (equals sign separator)
            LOCK_FILES="$LOCK_FILES ${1#--lock=}"
            shift
            ;;
        *)
            # Not a lock argument - pass to make
            MAKE_ARGS="$MAKE_ARGS $1"
            shift
            ;;
    esac
done

# ============================================================================
# MAIN EXECUTION: BUILD WITH LOCKS
# ============================================================================
# If lock files were specified, acquire them before building.
# This ensures:
#   1. Builds don't execute concurrently on interdependent components
#   2. Lock cleanup happens automatically on any exit (success/failure/signal)
#   3. The build wrapper preserves the make process exit code

if [ -n "$LOCK_FILES" ]; then
    FD=8  # File descriptor counter (starts at 8, increments per lock)

    # CLEANUP HANDLER: Executed on EXIT, INT, TERM signals
    # Removes all acquired lock files, unblocking waiting builds
    cleanup_lock() {
        for LOCK_FILE in $LOCK_FILES; do
            LOCK_PATH="/workspace/$LOCK_FILE"
            log_message "[$COMPONENT] 🧹 Cleaning up lock file: $LOCK_PATH"
            rm -f "$LOCK_PATH"
        done
    }
    
    # Register cleanup as exit handler for all termination scenarios
    trap cleanup_lock EXIT INT TERM

    # LOCK ACQUISITION: Sequential flock on each lock file
    # Each lock uses a unique file descriptor (8, 9, 10, ...)
    # flock -x = exclusive lock (wait if already held)
    for LOCK_FILE in $LOCK_FILES; do
        LOCK_PATH="/workspace/$LOCK_FILE"

        # Check if lock file already exists (another build in progress)
        if [ -f "$LOCK_PATH" ]; then
            log_message "[$COMPONENT] ⏳ Waiting for lock file: $LOCK_PATH"
            log_message "[$COMPONENT]    A previous build is currently in progress. This build will resume once it completes..."
        fi

        # Assign unique file descriptor and open lock file
        FD=$((FD + 1))
        eval "exec $FD>\"$LOCK_PATH\""

        # Attempt exclusive lock - blocks until available
        flock -x $FD || {
            log_message "[$COMPONENT] ✗ Failed to acquire lock: $LOCK_FILE"
            log_message "[$COMPONENT]    Possible causes: file system error, permission denied, or timeout"
            exit 1
        }

        log_message "[$COMPONENT] ✓ Lock acquired: $LOCK_PATH"
    done

    log_message "[$COMPONENT]    All locks acquired. Starting build..."

    # EXECUTE BUILD with all locks held
    do_build
    BUILD_STATUS=$?

    # Report build result
    if [ $BUILD_STATUS -eq 0 ]; then
        log_message "[$COMPONENT] ✓ Build completed successfully!"
    else
        log_message "[$COMPONENT] ✗ Build failed with exit code $BUILD_STATUS"
    fi

    # Trap cleanup will execute automatically here
    exit $BUILD_STATUS

# ============================================================================
# ALTERNATIVE EXECUTION: BUILD WITHOUT LOCKS
# ============================================================================
# If no lock files specified, execute build directly (simpler path)

else
    log_message "[$COMPONENT] Starting build (no locks required)..."
    do_build
    exit $?
fi

