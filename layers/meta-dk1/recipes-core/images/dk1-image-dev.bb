SUMMARY = "DK1 image with the GameSir driver and USB/input debugging tools"

# core-image-minimal, not core-image-base: minimal is already fully cached
# from the first build, so this adds minutes rather than another long one.
# It still pulls in udev, which the driver needs for modalias autoloading.
require recipes-core/images/core-image-minimal.bb

IMAGE_INSTALL += " \
    kernel-module-gamesir-k1 \
    usbutils \
    evtest \
"

# ssh so a rebuilt .ko can be scp'd over instead of reflashing the card,
# and an empty root password to keep the loop short. Dev image only.
IMAGE_FEATURES += "ssh-server-dropbear debug-tweaks"
