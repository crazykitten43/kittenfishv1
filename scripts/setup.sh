#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
make
.venv/bin/python scripts/fetch_network.py
chmod +x kittenfish
