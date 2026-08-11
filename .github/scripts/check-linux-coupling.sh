#!/usr/bin/env bash
# Freezes the set of files permitted to depend on Linux.
#
# Cookie is moving to its own kernel. The risk in a migration like that is not
# the files already on the list - those are known and scheduled - it is the
# tenth file, added later by someone reaching for a Linux facility because it
# was there. Each one is small and reasonable on its own, and together they are
# why a port never finishes.
#
# So the list below is a ceiling, exactly like the kernel system call surface.
# A file may leave it. Nothing may join it without deleting a line here, which
# is a decision someone has to make deliberately and defend in review.
#
# Usage: check-linux-coupling.sh <label>
set -uo pipefail

label="${1:-linux-coupling}"

# Files permitted to include Linux-specific headers. Every entry is a component
# with a named replacement in docs/M7_2_DELINUX.md.
permitted=(
    "core/osaccessibility/include/os/accessibility/broker.hpp"
    "core/osapp/include/os/app/bootstrap.hpp"
    "core/osapp/include/os/app/service_session.hpp"
    "core/osdisplay/src/buffer_linux.cpp"
    "core/osipc/include/os/ipc/channel.hpp"
    "core/osipc/src/channel_linux.cpp"
    "core/ossandbox/include/os/sandbox/sandbox.hpp"
    "core/ossandbox/include/os/sandbox/substrate.hpp"
    "core/ossandbox/src/sandbox_linux.cpp"
    "core/ossandbox/src/substrate_linux.cpp"
    "core/osservice/include/os/service/identity.hpp"
    "system/app_manager/include/os/app/accessibility_control.hpp"
    "system/app_manager/include/os/app/shell_lifecycle_control.hpp"
    "system/app_manager/src/manager_linux.cpp"
    "system/app_manager/src/runtime_session.cpp"
    "system/services/compositor/src/service.cpp"
    "system/supervisor/include/os/supervisor/process_authority.hpp"
    "system/supervisor/src/process_authority_linux.cpp"
    "system/supervisor/src/supervisor.cpp"
)

# Headers that mean "this file is talking to Linux". Deliberately narrow: plain
# POSIX that a Cookie libc will provide anyway is not coupling, and flagging it
# would train people to ignore this check.
pattern='sys/socket\.h|SOCK_SEQPACKET|SCM_RIGHTS|SCM_CREDENTIALS|linux/|sys/prctl\.h|sys/syscall\.h|seccomp'

found="$(grep -rlE -- "$pattern" core system --include='*.cpp' --include='*.hpp' 2>/dev/null | sed 's#\\#/#g' | sort)"

report=""
violations=0
for file in $found; do
    allowed=0
    for entry in "${permitted[@]}"; do
        if [ "$file" = "$entry" ]; then allowed=1; break; fi
    done
    if [ "$allowed" -eq 0 ]; then
        report="${report}NEW  ${file}%0A"
        violations=$((violations + 1))
    fi
done

# A file that has shed its Linux dependency should leave the list, or the
# ceiling stops describing anything. This is a notice rather than a failure:
# removing coupling must never be the thing that turns a build red.
for entry in "${permitted[@]}"; do
    still=0
    for file in $found; do
        if [ "$file" = "$entry" ]; then still=1; break; fi
    done
    if [ "$still" -eq 0 ]; then
        report="${report}FREED ${entry} - remove it from the permitted list%0A"
    fi
done

count="$(printf '%s' "$found" | grep -c . || true)"
report="${report}${count} of a permitted ${#permitted[@]} files depend on Linux"

if [ "$violations" -ne 0 ]; then
    echo "::error title=Linux coupling (${label})::${report}"
    exit 1
fi

echo "::notice title=Linux coupling (${label})::${report}"
