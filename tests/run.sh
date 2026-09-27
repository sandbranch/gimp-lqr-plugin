#!/bin/sh
# Builds the Liquid Rescale plug-in into tests/output and tests it inside
# the Flatpak GIMP without a window (see lqr-test.py). GIMP runs with a
# throwaway profile in tests/output/profile (GIMP3_DIRECTORY), where the
# plug-in is installed: your own GIMP profile and plug-ins are not used or
# changed, and a running GIMP of yours does not matter. The build and
# GIMP run isolated from your folders (tests/isolate.sh, with
# gimp-plugin-devtools/gimp-run.sh): HOME and the XDG folders inside the
# Flatpak point into tests/output/gimp-home, so nothing lands in
# ~/.var/app/org.gimp.GIMP either. Before and after, it lists your
# folders of GIMP and the other apps (gimp-plugin-devtools/snapshot.sh)
# and fails if anything there changed.
#
#   tests/run.sh          build, install into the test profile, test
#   tests/run.sh --asan   the same with AddressSanitizer and UBSan
#   LQR_ONLY=mask tests/run.sh   only the cases whose names match
#   LQR_LEAKS=1 tests/run.sh --asan   also report memory leaks
#
# Prints PASS or FAIL for each case and exits non-zero if any case fails,
# or if the plug-in printed warnings, criticals or sanitizer reports.
# Needs ../gimp-plugin-devtools (or GIMP_PLUGIN_DEVTOOLS) for the build.
here=$(cd "$(dirname "$0")" && pwd)
src=$(dirname "$here")
out=$here/output
devtools=${GIMP_PLUGIN_DEVTOOLS:-$src/../gimp-plugin-devtools}
GIMP_RUN_HOME=$out/gimp-home
export GIMP_RUN_HOME
# shellcheck source=SCRIPTDIR/isolate.sh
. "$here/isolate.sh"
mkdir -p "$out"
snapshot_take "$out/snapshot-before.txt"

build=$out/build
profile=$out/profile
setup_args=
run_args=
if [ "$1" = --asan ]; then
    build=$out/build-asan
    profile=$out/profile-asan
    setup_args="-Db_sanitize=address,undefined -Db_lundef=false"
    # the sanitizer runtimes are in the SDK, which --devel runs GIMP with.
    # Leak reports (LQR_LEAKS=1) are off by default: libgimp and liblqr
    # leak a few small blocks each run that are not the plug-in's.
    run_args="--devel --env=ASAN_OPTIONS=log_path=$out/sanitizer/asan:detect_leaks=${LQR_LEAKS:-0} \
      --env=UBSAN_OPTIONS=log_path=$out/sanitizer/ubsan:print_stacktrace=1"
fi
rm -rf "$out/sanitizer"
mkdir -p "$out/sanitizer"
plugindir=$profile/plug-ins/gimp-lqr-plugin
log=$out/test.log
mkdir -p "$out"

if [ ! -d "$build" ]; then
    "$devtools/gimp-build.sh" "$src" "meson setup '$build' $setup_args \
      -Dgimp_plugindir='$plugindir' -Dgimp_plugin_datadir='$plugindir'" \
      >"$out/setup.log" 2>&1 || { cat "$out/setup.log"; exit 1; }
fi
"$devtools/gimp-build.sh" "$src" "ninja -C '$build' install" \
  >"$out/build.log" 2>&1 || { cat "$out/build.log"; exit 1; }

# shellcheck disable=SC2086
gimp_run --timeout=1800 --flatpak $run_args --filesystem="$src" --env=GIMP3_DIRECTORY="$profile" \
  --env=LQR_ONLY="$LQR_ONLY" -- \
  gimp-console-3.2 --no-interface --no-data --batch-interpreter python-fu-eval \
  -b "exec(open('$here/lqr-test.py').read())" --quit >"$log" 2>&1

grep -E "^LQR|Traceback|^  File|Error" "$log"

status=0
grep -q "^LQR failures: 0$" "$log" || status=1
# messages of the plug-in (its process is named after it), and GIMP
# closing undo groups that the plug-in left open
if grep -E "gimp-lqr-plugin.*(WARNING|CRITICAL)|inconsistent state|plug-in-lqr.*(Warning|Message)" "$log"; then
    echo "LQR FAIL: warnings from the plug-in, see $log"
    status=1
fi
for report in "$out"/sanitizer/*; do
    [ -e "$report" ] || continue
    echo "LQR FAIL: sanitizer report $report"
    head -30 "$report"
    status=1
done
snapshot_check "$out/snapshot-before.txt" "LQR " || status=1
[ $status = 0 ] && echo "LQR all passed" || echo "LQR FAILED (log: $log)"
exit $status
