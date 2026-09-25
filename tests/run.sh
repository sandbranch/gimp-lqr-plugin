#!/bin/sh
# Tests the installed Liquid Rescale plug-in inside the Flatpak GIMP,
# without a window (see lqr-test.py). Install first (README), close GIMP.
here=$(cd "$(dirname "$0")" && pwd)
flatpak run --filesystem="$here" --command=gimp-console-3.2 org.gimp.GIMP \
  --no-interface --no-data --batch-interpreter python-fu-eval \
  -b "exec(open('$here/lqr-test.py').read())" --quit 2>&1 | grep -E "^LQR|Error|Traceback|line [0-9]"
