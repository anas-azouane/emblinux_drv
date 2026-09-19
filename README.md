# emblinux_drv

A Yocto build for the STM32MP157D-DK1 and an out-of-tree kernel driver for a
GameSir-K1 USB gamepad.

The board half is a small BSP layer, `meta-dk1`, sitting on top of ST's
`meta-st-stm32mp`. The driver half is `gamesir-k1`, an `hid_driver` that
decodes the pad's HID reports itself and feeds its own input device rather
than letting `hid-generic` do the mapping.

Tested against Yocto 5.0.20 (scarthgap), kernel 6.6.129, on an
STM32MP157D-DK1 with a GameSir-K1 (USB `3537:1082`).

## What's here

    layers/meta-dk1/
      conf/machine/stm32mp157d-dk1.conf   the machine
      wic/sdcard-stm32mp157d-dk1.wks.in   SD card layout
      recipes-kernel/gamesir-k1/          the driver and its recipe
      recipes-core/images/dk1-image-dev.bb
    docs/gamesir-k1-protocol.md           decoded HID protocol
    build/conf/                           local.conf and bblayers.conf
    TUTORIAL.md                           notes from working through Yocto
    setup-env.sh

The three upstream layers are not vendored. See below for fetching them.

## Why a custom machine

ST ships `stm32mp15-disco`, which sounds right but targets the
**STM32MP157F-DK2**. The DK2 has a DSI panel and a BCM43xx Wi-Fi/BT module
that the DK1 does not, and its device tree assumes both. Booting DK2 device
trees on a DK1 gets you a half-working system.

`stm32mp157d-dk1.conf` requires ST's machine and overrides only what differs:
the device tree, and the `wifi`/`bluetooth` machine features. That keeps the
delta readable and survives ST bumping their layer.

It also fixes a gap in ST's `WKS_FILE_DEPENDS`. Nothing there orders
`fip-stm32mp` before `do_image_wic`, and `virtual-optee-os` is gated on a
`bb.utils.contains(BOOTSCHEME_LABELS, 'optee')` test that does not match this
board's `opteemin`. Neither shows up upstream because ST ship the wic lines
commented out. With wic enabled, the build dies on the very last task looking
for a FIP that was never deployed.

## Setting up

Host packages, on Debian 12:

    sudo apt install -y gawk wget git diffstat unzip texinfo gcc \
      build-essential chrpath socat cpio python3 python3-pip python3-pexpect \
      xz-utils debianutils iputils-ping python3-git python3-jinja2 \
      python3-subunit zstd liblz4-tool file locales libacl1 bmap-tools

Fetch the upstream layers into `layers/`. These are the revisions this was
built against:

    cd layers
    git clone -b scarthgap https://git.yoctoproject.org/poky                       # b2c16f1
    git clone -b scarthgap https://github.com/openembedded/meta-openembedded.git   # b5874ea
    git clone -b scarthgap https://github.com/STMicroelectronics/meta-st-stm32mp.git # 99e728c

`build/conf/bblayers.conf` and `local.conf` carry absolute paths under
`/home/debext/yoctotst`. Fix those to match wherever you cloned this.

## Building

    source setup-env.sh
    bitbake dk1-image-dev

The first build takes a few hours and about 50 GB. `downloads/` and
`sstate-cache/` live outside `build/`, so `rm -rf build/tmp` is cheap to
recover from.

`local.conf` sets `BB_NUMBER_THREADS` and `PARALLEL_MAKE` to 4 each. They
multiply, so that is 16 concurrent jobs. 8x8 exhausted 15 GB of RAM when
rust-native landed on top of `do_rootfs`. `BB_PRESSURE_MAX_MEMORY` and
`BB_PRESSURE_MAX_IO` are also set, which lets BitBake read the kernel's PSI
counters and stop launching new tasks under pressure instead of falling over.

## Flashing

    cd build/tmp/deploy/images/stm32mp157d-dk1
    sudo umount /dev/sdX* 2>/dev/null
    sudo bmaptool copy dk1-image-dev-stm32mp157d-dk1.rootfs.wic /dev/sdX

`sdX` is the whole card, not a partition. Check it twice with `lsblk`.

Card in the slot underneath, both boot DIP switches ON, USB-C into CN6
(ST-LINK), then `picocom -b 115200 /dev/ttyACM0` and press RESET. You should
get TF-A, OP-TEE, U-Boot, then a login prompt. Root, no password.

## The driver

The pad calls itself "GameSir-K1 Controller for Xbox" but does not enumerate
as XInput. Both its interfaces are ordinary HID, so `hid-generic` binds and
produces four input devices. `gamesir-k1` is an `hid_driver` whose `id_table`
outranks generic.

Interface 0 is the gamepad and the driver takes it, clearing
`HID_CONNECT_HIDINPUT` so `hid-input` does not build a competing input device,
then decoding report 5 in `.raw_event`. Interface 1 is a keyboard/mouse/
consumer composite for the media keys, which `hid-generic` handles correctly,
so the driver hands it back.

Report 5 is 10 bytes: 15 button bits, a 4-bit hat with a null state, four
8-bit axes, two 8-bit triggers. Full decode in `docs/gamesir-k1-protocol.md`.

Two things the descriptor gets wrong, both found with `evtest`:

- Buttons 3 and 6 are declared but never fire. Only 13 of the 15 exist.
- X and Y are not where the usual mapping puts them. `input-event-codes.h`
  aliases `BTN_X` to `BTN_NORTH` and `BTN_Y` to `BTN_WEST`, which is a
  SNES-era layout. On an Xbox pad X is west and Y is north.
  `Documentation/input/gamepad.rst` says report by position, not by label.

On the board, the module is loaded from `/etc/modules-load.d/`, and the
`MODULE_DEVICE_TABLE` alias covers hotplug:

    lsmod | grep gamesir
    evtest        # pick "GameSir-K1 Gamepad"

### Building the driver against a host kernel

Useful for checking the decode without reflashing:

    sudo apt install linux-headers-$(uname -r)
    cd layers/meta-dk1/recipes-kernel/gamesir-k1/files
    make KERNEL_SRC=/lib/modules/$(uname -r)/build
    sudo insmod gamesir-k1.ko

`hid-generic` will already hold the device; replug the pad, or move it over
by hand:

    echo -n '0003:3537:1082.0003' | sudo tee /sys/bus/hid/drivers/hid-generic/unbind
    echo -n '0003:3537:1082.0003' | sudo tee /sys/bus/hid/drivers/gamesir-k1/bind

## Note on the images

Both images set `debug-tweaks`, which means an empty root password and a
console login. That is fine for a board on a desk and wrong for anything else.
Drop it from `IMAGE_FEATURES` before this goes anywhere real.

## License

The driver is GPL-2.0-only. The Yocto metadata is MIT, matching OE-Core.
