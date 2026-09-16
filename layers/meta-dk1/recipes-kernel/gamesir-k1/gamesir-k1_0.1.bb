SUMMARY = "Input driver for the GameSir-K1 USB gamepad"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

inherit module

SRC_URI = "file://Makefile \
           file://gamesir-k1.c \
"

# scarthgap has no UNPACKDIR; file:// lands directly in ${WORKDIR}.
S = "${WORKDIR}"

RPROVIDES:${PN} += "kernel-module-gamesir-k1"

# Writes /etc/modules-load.d/gamesir-k1.conf so the module is up at boot even
# with no pad plugged in. The MODULE_DEVICE_TABLE alias already covers hotplug;
# this just makes it visible in lsmod from the start.
KERNEL_MODULE_AUTOLOAD += "gamesir-k1"
