SUMMARY = "Forwards GameSir-K1 input to the M4 game over rpmsg"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/BSD-3-Clause;md5=550794465ba0ec5312d6919e203a55f9"

SRC_URI = "file://pad-bridge.py \
           file://fake-pad.py \
"

# scarthgap has no UNPACKDIR; file:// lands directly in ${WORKDIR}.
S = "${WORKDIR}"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 pad-bridge.py ${D}${bindir}/pad-bridge
    # Drives the game with no hardware attached, for testing the rpmsg path.
    install -m 0755 fake-pad.py ${D}${bindir}/fake-pad
}

# Stock python3 only, no third-party modules -- but Yocto splits the standard
# library into packages, so the ones used have to be named. Of the imports
# (argparse, errno, fcntl, os, select, stat, struct, sys, time) everything but
# fcntl lives in python3-core; fcntl is its own package and the scripts need it
# for the EVIOCGABS ioctl that reads each axis range. Without it the bridge
# dies on import with ModuleNotFoundError.
RDEPENDS:${PN} += "python3-core python3-fcntl kernel-module-gamesir-k1"
