#!/bin/sh
# The Unit Builder icons (ui/bicons/) are game art and are not in git. This used to copy them from the wiki site;
# now it extracts them from the installed game. Arguments go to extract_icons.py (e.g. --game <folder>).
set -e
here=$(cd "$(dirname "$0")" && pwd)
exec "${PYTHON:-python3}" "$here/extract_icons.py" "$@"
