#!/usr/bin/env bash
# Package the host executable and its corresponding working-tree source.
set -euo pipefail
export LC_ALL=C
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd -- "$repo"
if [[ $# != 2 ]]; then
    echo "Usage: $0 BINARY OUTPUT_DIRECTORY" >&2
    exit 2
fi
binary=$(realpath -- "$1")
output=$(realpath -m -- "$2")
version=$(sed -n 's/^#define THE_BUGGER_VERSION "\([0-9][0-9.]*\)"$/\1/p' include/the_bugger/version.h)
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo 'Invalid release version' >&2; exit 1; }
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || { echo 'Release packaging requires Linux x86-64' >&2; exit 1; }
[[ -x $binary ]] || { echo 'Missing executable' >&2; exit 1; }
readelf -h -- "$binary" | grep -q 'Machine:.*Advanced Micro Devices X86-64' || { echo 'Expected x86-64 ELF' >&2; exit 1; }
for command in git tar gzip sha256sum strip sort getconf; do
    command -v "$command" >/dev/null || { echo "Missing packaging tool: $command" >&2; exit 1; }
done
mkdir -p -- "$output"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
name="the-bugger-v${version}-linux-x86_64"
source_name="the-bugger-v${version}-source"
mkdir -- "$work/$name"
install -m 755 -- "$binary" "$work/$name/the-bugger"
strip --strip-debug -- "$work/$name/the-bugger"
install -m 644 LICENSE "$work/$name/LICENSE"
install -m 644 THIRD_PARTY_NOTICES "$work/$name/THIRD_PARTY_NOTICES"
# Include required release sources while they await their implementation commit.
git ls-files -z > "$work/files"
printf '%s\0' tools/package-release.sh tests/integration/test_setup_board.c \
    tests/integration/test_setup_board_tui.c >> "$work/files"
sort -zu "$work/files" -o "$work/files"
epoch=${SOURCE_DATE_EPOCH:-0}
[[ $epoch =~ ^[0-9]+$ ]] || { echo 'SOURCE_DATE_EPOCH must be a nonnegative integer' >&2; exit 1; }
tar --null --verbatim-files-from --files-from="$work/files" \
    --mtime="@$epoch" --owner=0 --group=0 --numeric-owner \
    --transform="s,^,${source_name}/," -cf - | gzip -n > "$work/$source_name.tar.gz"
{
    printf 'The Bugger %s\nHost: %s\nLibc: %s\n' "$version" "$(uname -srmo)" "$(getconf GNU_LIBC_VERSION)"
    printf '\nDirect runtime libraries (their distribution dependencies also apply):\n'
    readelf -d -- "$work/$name/the-bugger" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p'
    printf '\nGLIBC symbol versions required by the executable:\n'
    readelf --version-info -- "$work/$name/the-bugger" | grep -oE 'GLIBC_[0-9.]+' | sort -Vu
    printf '\nCompiler information from the ELF:\n'
    readelf -p .comment -- "$work/$name/the-bugger"
    printf '\nCorresponding source archive SHA-256:\n'
    (cd -- "$work" && sha256sum -- "$source_name.tar.gz")
} > "$work/$name/BUILD-INFO.txt"
cat > "$work/$name/RUNNING.txt" <<'TEXT'
The Bugger — Linux x86-64

Run ./the-bugger from a real terminal; --help and --version work without a TTY.
Use --demo for the simulated workspace. Native launch requires GDB 17 or newer
for literal arguments without a startup shell; remote sessions require a
target-compatible GDB.
OpenOCD is an optional external tool for supported embedded connections.
Install the runtime libraries listed in BUILD-INFO.txt, their dependencies, and
terminfo for your terminal. This dynamically linked build is tested on the
recorded build host; it does not promise compatibility with older distributions.
In tmux, enable mouse forwarding with: set -g mouse on

Keep the OpenOCD executable and script installation matched. An ESP-IDF shell
may export OPENOCD_SCRIPTS for Espressif OpenOCD. When using the system OpenOCD,
start with: env -u OPENOCD_SCRIPTS ./the-bugger
Or add /usr/share/openocd/scripts through Profile / OpenOCD search directories
when using that system installation.
Inspect configuration scripts before connecting; initialization can affect hardware.

Physical workflows cover ARM/Cortex-M (NUCLEO-F446RE with onboard ST-LINK),
Xtensa (ESP32-S3) and RISC-V RV32 (ESP32-C6FH4), with the configurations and
capability-specific results documented in the source archive.
No compatibility claim is made for unverified targets or tools.

Saving a setup writes profile-store schema 3. Older builds cannot read that
store; retain a backup before saving if an older build is still needed.
Loading an older store alone does not migrate it. No automatic downgrade exists.

Licensed under GPLv3; see LICENSE. Full build instructions, user documentation,
tests and corresponding source accompany this binary in the source archive.
TEXT
# Exercise the packaged executable outside the source/build tree.
packaged_version=$(cd -- "$work/$name" && ./the-bugger --version)
[[ $packaged_version == "the-bugger $version" ]] || { echo 'Executable/source version mismatch' >&2; exit 1; }
printf '%s\n' "$packaged_version"
(cd -- "$work/$name" && ./the-bugger --help >/dev/null)
tar --sort=name --mtime="@$epoch" --owner=0 --group=0 --numeric-owner \
    -C "$work" -cf - "$name" | gzip -n > "$work/$name.tar.gz"
install -m 644 -- "$work/$name.tar.gz" "$work/$source_name.tar.gz" "$output/"
(cd -- "$output" && sha256sum -- "$name.tar.gz" "$source_name.tar.gz" > SHA256SUMS)
printf 'Prepared release artifacts in %s\n' "$output"
printf 'Artifacts are not published or release-approved.\n'
