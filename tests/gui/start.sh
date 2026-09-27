#!/bin/sh
# Opens the Liquid Rescale dialog in the Flatpak GIMP on a Broadway display
# (http://127.0.0.1:8085/), to look at it with
# gimp-plugin-devtools/gui/cdp.mjs. The plug-in is the one tests/run.sh
# built and installed into its throwaway profile (tests/output/profile);
# run that first. GIMP runs isolated from your own folders
# (tests/isolate.sh), with the throwaway home of tests/run.sh, so a GIMP
# of yours does not matter. broadwayd stops when GIMP quits, also when
# GIMP fails.
here=$(cd "$(dirname "$0")" && pwd)
tests=$(dirname "$here")
src=$(dirname "$tests")
[ -d "$tests/output/profile/plug-ins/gimp-lqr-plugin" ] ||
  { echo "run tests/run.sh first: it installs the plug-in into tests/output/profile" >&2; exit 1; }
GIMP_RUN_HOME=${GIMP_RUN_HOME:-$tests/output/gimp-home}
# shellcheck source=SCRIPTDIR/../isolate.sh
. "$tests/isolate.sh"
gimp_run --flatpak --filesystem="$src" --env=GDK_BACKEND=broadway --env=BROADWAY_DISPLAY=:5 \
  --env=GIMP3_DIRECTORY="$tests/output/profile" -- sh -c \
  "broadwayd --port 8085 :5 & bw=\$!; trap 'kill \$bw' EXIT; sleep 2; \
   gimp-3.2 --no-splash \
   --batch-interpreter python-fu-eval -b \"exec(open('$here/open-dialog.py').read())\""
