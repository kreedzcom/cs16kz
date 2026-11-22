#!/bin/bash
set -euo pipefail

zig build -Dtarget=x86-linux-gnu -Doptimize=ReleaseFast "$@"
