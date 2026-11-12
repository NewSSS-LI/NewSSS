#!/bin/bash
# Prints the board's device if exactly one 32GB Cosmos+ namespace is present.
# Exit status 0 = found. Used by the dev PC after loading firmware.
. "$(dirname "$0")/common.sh"
find_board
echo "OK $DEV $(basename "$(readlink -f /sys/block/$(basename "$DEV")/device/device)")"
