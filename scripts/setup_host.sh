#!/usr/bin/env bash
# Host setup for TraceArbiter.
# Default: report only.
#   scripts/setup_host.sh --apply
#     uses sudo once. Does not store a password.
#     Grants the invoking user via tracefs uid=/gid=/mode= (kernel
#     mount options). Does not chmod 777 / world-writable.

set -euo pipefail

TRACEFS=/sys/kernel/tracing
APPLY=0
PERSIST=1

usage() {
    echo "usage: $0 [--apply] [--no-persist]" >&2
    exit 2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --apply) APPLY=1 ;;
        --no-persist) PERSIST=0 ;;
        -h|--help) usage ;;
        *) usage ;;
    esac
    shift
done

echo "kernel: $(uname -r)"
echo "compiler: $(${CXX:-g++} --version | head -n1)"

if command -v cmake >/dev/null 2>&1; then
    echo "cmake: $(cmake --version | head -n1)"
else
    echo "cmake: missing (install CMake >= 3.24, or python3 -m pip install cmake)"
fi

if [[ ! -e "$TRACEFS" ]]; then
    echo "tracefs: missing at $TRACEFS"
    echo "status: BLOCKED_TRACEFS_PERMISSION"
    exit 1
fi

already_writable_for() {
    local user="$1"
    local probe="$TRACEFS/instances/.ta_perm_probe.$$"
    as_user() {
        if [[ "$(id -u)" -eq 0 && "$user" != root ]]; then
            runuser -u "$user" -- "$@"
        else
            "$@"
        fi
    }
    as_user test -w "$TRACEFS/tracing_on" || return 1
    as_user test -w "$TRACEFS/instances" || return 1
    if ! as_user mkdir "$probe"; then
        return 1
    fi
    local ok=0
    if as_user test -w "$probe/tracing_on"; then
        ok=1
    fi
    as_user rmdir "$probe" 2>/dev/null || rmdir "$probe" 2>/dev/null || true
    [[ "$ok" -eq 1 ]]
}

TARGET_USER="${SUDO_USER:-$(id -un)}"
if [[ "$(id -u)" -eq 0 && -n "${SUDO_USER:-}" ]]; then
    TARGET_USER="$SUDO_USER"
fi

echo "tracefs: $TRACEFS"
ls -ld "$TRACEFS" 2>/dev/null || true

if already_writable_for "$TARGET_USER"; then
    echo "status: READY (tracefs instances writable for $TARGET_USER)"
    exit 0
fi

if [[ "$APPLY" -ne 1 ]]; then
    echo "status: BLOCKED_TRACEFS_PERMISSION"
    echo
    echo "This user cannot write tracing instances."
    echo "Re-run with --apply (sudo). That remounts tracefs with"
    echo "uid=$(id -u) gid=$(id -g) so $(id -un) can use $TRACEFS."
    echo "It does not chmod 777."
    echo "A systemd drop-in keeps the gid after reboot unless --no-persist."
    exit 1
fi

if [[ "$(id -u)" -eq 0 ]]; then
    TARGET_USER="${SUDO_USER:-idblab}"
    TARGET_GID="$(id -g "$TARGET_USER")"
    TARGET_UID="$(id -u "$TARGET_USER")"
    TARGET_GROUP="$(id -gn "$TARGET_USER")"
    echo "apply as root for user=$TARGET_USER uid=$TARGET_UID gid=$TARGET_GID ($TARGET_GROUP)"

    # Same superblock may also be mounted at debug/tracing; remount both.
    mount -o "remount,uid=${TARGET_UID},gid=${TARGET_GID},mode=750" "$TRACEFS" || true
    if findmnt -n /sys/kernel/debug/tracing >/dev/null 2>&1; then
        mount -o "remount,uid=${TARGET_UID},gid=${TARGET_GID},mode=750" /sys/kernel/debug/tracing || true
    fi
    echo "mount: $(findmnt -no OPTIONS "$TRACEFS" || true)"
    echo "root_stat: $(stat -c '%a %U %G' "$TRACEFS" 2>/dev/null || true)"

    granted=0
    if runuser -u "$TARGET_USER" -- test -w "$TRACEFS/tracing_on" && \
       runuser -u "$TARGET_USER" -- test -w "$TRACEFS/instances"; then
        granted=1
    fi

    if [[ "$granted" -ne 1 ]]; then
        echo "mount uid/gid did not grant writes; group+owner chmod fallback (not 777)"
        chown -R "${TARGET_UID}:${TARGET_GID}" "$TRACEFS" 2>/dev/null || true
        chmod -R u+rwX,g+rwX "$TRACEFS" || true
        echo "root_stat_after_fallback: $(stat -c '%a %U %G' "$TRACEFS" 2>/dev/null || true)"
    fi

    if [[ "$PERSIST" -eq 1 ]]; then
        dropin=/etc/systemd/system/sys-kernel-tracing.mount.d
        mkdir -p "$dropin"
        cat > "$dropin/tracearbiter-gid.conf" <<EOF
# Written by TraceArbiter scripts/setup_host.sh --apply.
# Grants gid ${TARGET_GID} (${TARGET_GROUP}) on /sys/kernel/tracing after reboot.
[Mount]
Options=nosuid,nodev,noexec,uid=${TARGET_UID},gid=${TARGET_GID},mode=750
EOF
        systemctl daemon-reload
        echo "persist: $dropin/tracearbiter-gid.conf"
    fi

    if runuser -u "$TARGET_USER" -- test -w "$TRACEFS/tracing_on" && \
       runuser -u "$TARGET_USER" -- test -w "$TRACEFS/instances"; then
        echo "status: READY (tracefs instances writable)"
        ls -ld "$TRACEFS" "$TRACEFS/instances"
        exit 0
    fi

    echo "status: BLOCKED_TRACEFS_PERMISSION"
    echo "gid remount and group chmod were not enough on this kernel." >&2
    ls -ld "$TRACEFS" "$TRACEFS/instances" 2>/dev/null || true
    findmnt "$TRACEFS" || true
    exit 1
fi

echo "re-exec with sudo (password is not stored)"
persist_flag=()
if [[ "$PERSIST" -eq 0 ]]; then
    persist_flag=(--no-persist)
fi
exec sudo --preserve-env=TRACEARBITER_TRACEFS_GID -- "$0" --apply "${persist_flag[@]}"
