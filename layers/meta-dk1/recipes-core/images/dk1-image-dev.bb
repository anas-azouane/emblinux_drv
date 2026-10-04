SUMMARY = "DK1 image with the GameSir driver and USB/input debugging tools"

# core-image-minimal, not core-image-base: minimal is already fully cached
# from the first build, so this adds minutes rather than another long one.
# It still pulls in udev, which the driver needs for modalias autoloading.
require recipes-core/images/core-image-minimal.bb

IMAGE_INSTALL += " \
    kernel-module-gamesir-k1 \
    usbutils \
    evtest \
    i2c-tools \
    lrzsz \
    m4projects-stm32mp1 \
    stickman-m4 \
    pad-bridge \
"

# i2c-tools is for the OLED on the Arduino header (I2C5, PA11/PA12). Boot the
# stm32mp157d-dk1-a7-examples DTB to get that bus under Linux -- the stock DK1
# tree leaves i2c5 disabled, and the m4-examples tree hands it to the M4.
# i2ctransfer is enough to init the panel from the shell and prove the wiring
# before any firmware exists.

# lrzsz gives us rz on the target, so firmware can come over the serial
# console with picocom's Ctrl-A Ctrl-S instead of pulling the SD card every
# time. There is no ethernet on this setup, and the M4 firmware gets rebuilt
# a lot. ~11 KB/s at 115200, so a 200 KB ELF is about 20 seconds.

# dropbear is kept for the day an ethernet cable does turn up; the empty root
# password keeps the loop short. Dev image only.
IMAGE_FEATURES += "ssh-server-dropbear debug-tweaks"
