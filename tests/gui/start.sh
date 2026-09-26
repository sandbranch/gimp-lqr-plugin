#!/bin/sh
# Opens the Liquid Rescale dialog in the Flatpak GIMP on a Broadway display
# (http://127.0.0.1:8085/), to look at it with
# gimp-plugin-devtools/gui/cdp.mjs. Close GIMP first. broadwayd stops when
# GIMP quits, also when GIMP fails.
here=$(cd "$(dirname "$0")" && pwd)
flatpak run --filesystem="$here" --env=GDK_BACKEND=broadway --env=BROADWAY_DISPLAY=:5 \
  --command=sh org.gimp.GIMP -c \
  "broadwayd --port 8085 :5 & bw=\$!; trap 'kill \$bw' EXIT; sleep 2; \
   gimp-3.2 --no-splash \
   --batch-interpreter python-fu-eval -b \"exec(open('$here/open-dialog.py').read())\""
