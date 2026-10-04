SUMMARY = "Cortex-M4 firmware: I2C display probe plus a game steered from Linux"
DESCRIPTION = "Scans I2C5 for a display, self-tests it, then runs a game that \
Linux drives over rpmsg. Supports SSD1306/SH1106 OLEDs and HD44780-behind-PCF8574 \
LCDs. Does not use OpenAMP: the rpmsg layer and vrings are implemented directly."
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://License.md;md5=532c0d9fc2820ec1304ab8e0f227acc7"

# The firmware's own sources are local; the git fetch is only for ST's HAL and
# CMSIS headers, pinned to the revision ST's m4projects recipe uses.
SRC_URI = "git://github.com/STMicroelectronics/STM32CubeMP1.git;protocol=https;branch=master \
           file://Makefile \
           file://inc \
           file://src \
           file://ld \
"
SRCREV = "525d2499658d817a9e669eb17e66390906954895"
S = "${WORKDIR}/git"
B = "${WORKDIR}/build"

DEPENDS = "gcc-arm-none-eabi-native"
COMPATIBLE_MACHINE = "(stm32mp1common)"
PACKAGE_ARCH = "${MACHINE_ARCH}"

# Exactly one game is compiled in. snake is the other option.
GAME ?= "stickman"

M4_PREFIX = "${RECIPE_SYSROOT_NATIVE}${datadir}/gcc-arm-none-eabi/bin/arm-none-eabi-"
M4_TARGET = "i2c_screen_cm4"

do_configure() {
    # The upstream Makefile expects inc/ src/ ld/ beside it and writes into
    # ./build, so give it that layout rather than patching paths into it.
    rm -rf ${B}
    install -d ${B}
    cp -r ${WORKDIR}/Makefile ${WORKDIR}/inc ${WORKDIR}/src ${WORKDIR}/ld ${B}/
}

do_compile() {
    # Yocto's flags target A7 userspace and break a bare-metal M4 link.
    unset CFLAGS CPPFLAGS CXXFLAGS LDFLAGS AS LD

    # Command-line assignments win over the Makefile's own, so it stays
    # unmodified from the standalone project.
    oe_runmake -C ${B} \
        CUBE=${S} \
        GAME=${GAME} \
        CROSS=${M4_PREFIX}
}

do_install() {
    install -d ${D}${nonarch_base_libdir}/firmware

    # -g3 makes this a couple of megabytes and there is no ethernet here, so
    # every byte of a deploy crosses a 115200 serial line. remoteproc reads
    # only the loadable segments and the resource table.
    ${M4_PREFIX}strip --strip-debug --strip-unneeded \
        -o ${D}${nonarch_base_libdir}/firmware/${M4_TARGET}.elf \
        ${B}/build/${M4_TARGET}.elf
}

FILES:${PN} = "${nonarch_base_libdir}/firmware"

# A Cortex-M4 ELF is not an A7 userspace binary.
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
INHIBIT_SYSROOT_STRIP = "1"
EXCLUDE_FROM_SHLIBS = "1"
INSANE_SKIP:${PN} += "arch ldflags file-rdeps staticdev buildpaths"
