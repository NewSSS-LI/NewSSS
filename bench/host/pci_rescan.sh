#!/bin/bash
# Called by the dev PC around a firmware load.
#   pci_rescan.sh remove : unmount and detach the board before the board resets
#   pci_rescan.sh rescan : rescan PCI after the new firmware is up
H="$(dirname "$0")"
. "$H/common.sh"
case "$1" in
remove)
	mountpoint -q "$MNT" && sudo -n /usr/bin/umount "$MNT"
	for b in /sys/block/nvme*n1; do
		[ -e "$b" ] || continue
		[ "$(sed "s/ *$//" "$b/device/model" 2>/dev/null)" = "$EXPECT_MODEL" ] || continue
		echo 1 | sudo -n /usr/bin/tee "$(readlink -f "$b/device/device")/remove" > /dev/null
	done
	echo removed ;;
rescan)
	echo 1 | sudo -n /usr/bin/tee /sys/bus/pci/rescan > /dev/null
	sleep 10
	"$H/check_board.sh" ;;
*)
	echo "usage: $0 remove|rescan" >&2; exit 2 ;;
esac
