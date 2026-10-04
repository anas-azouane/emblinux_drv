# Replicating this from nothing

Every step, in the order it needs doing, with the traps marked where you would
hit them. Written after the fact, so the dead ends are left out and the things
that cost time are called out instead.

## What you need

- STM32MP157D-DK1 (STM32MP157A-DK1 works; change one device tree name)
- microSD card, 8 GB or more
- **USB-C power supply able to do 5 V / 3 A.** Not a laptop port. The ST-LINK
  connector powers the debug circuit only, and its LED tells you nothing about
  whether the PMIC brought the core rails up. Two separate occasions here looked
  like a dead board and were this.
- SSD1306 128x64 I2C OLED
- GameSir-K1 USB gamepad, or anything that enumerates as HID with a hat switch
- A Linux host with ~60 GB free. Debian 12 here, 8 cores, 15 GB RAM.

## 1. Host packages

    sudo apt install -y gawk wget git diffstat unzip texinfo gcc \
      build-essential chrpath socat cpio python3 python3-pip python3-pexpect \
      xz-utils debianutils iputils-ping python3-git python3-jinja2 \
      python3-subunit zstd liblz4-tool file locales libacl1 \
      bmap-tools lrzsz picocom

The last three are for flashing, serial file transfer and the console. Yocto
refuses to build under a non-UTF-8 locale.

## 2. Layers

    mkdir -p ~/yoctotst/layers && cd ~/yoctotst/layers
    git clone -b scarthgap https://git.yoctoproject.org/poky                        # b2c16f1
    git clone -b scarthgap https://github.com/openembedded/meta-openembedded.git    # b5874ea
    git clone -b scarthgap https://github.com/STMicroelectronics/meta-st-stm32mp.git # 99e728c

scarthgap is Yocto 5.0 LTS, which is what OpenSTLinux 6.x is built on and what
ST's layer declares compatible. The commits are what this was built against;
scarthgap moves, so pin if you want the same result.

`meta-st-stm32mp` needs `meta-oe` and `meta-python`, which is why
meta-openembedded is there. It does not need meta-arm: TF-A, OP-TEE and U-Boot
all live in ST's layer.

## 3. The machine, and why not ST's

ST ship `stm32mp15-disco`, which sounds right and is wrong: it targets the
**STM32MP157F-DK2**. The DK2 has a DSI panel and a BCM43xx Wi-Fi/BT module the
DK1 does not, and its device tree assumes both. DK2 device trees on a DK1 give
you a half-working system.

`layers/meta-dk1/conf/machine/stm32mp157d-dk1.conf` requires ST's machine and
overrides only the differences, so the delta stays readable and survives ST
bumping their layer. See the file; the parts that matter are the device tree
name and dropping the `wifi`/`bluetooth` machine features.

It also fixes a real gap in ST's `WKS_FILE_DEPENDS`. Nothing there orders
`fip-stm32mp` before `do_image_wic`, and `virtual-optee-os` is gated on

    bb.utils.contains('BOOTSCHEME_LABELS', 'optee', ...)

which does not match this board's `opteemin`. Neither shows up upstream because
ST ship the wic lines commented out. With wic enabled the build dies on the very
last task of a three-hour run, looking for a FIP that was never deployed.

## 4. Build configuration

Copy `build/conf/local.conf` and `build/conf/bblayers.conf` from this repo and
fix the absolute paths in both.

One setting is worth understanding rather than copying. `BB_NUMBER_THREADS` and
`PARALLEL_MAKE` **multiply**: 8 and `-j 8` is up to 64 concurrent compiler jobs.
That exhausted 15 GB when rust-native landed on top of `do_rootfs` and killed
the build at task 4034 of 4078. Yocto wants roughly 2 GB per concurrent job, so
4 x 4 is the ceiling here. `BB_PRESSURE_MAX_MEMORY` and `BB_PRESSURE_MAX_IO` are
also set, which lets BitBake read the kernel's PSI counters and stop launching
new tasks under pressure instead of falling over.

`DL_DIR` and `SSTATE_DIR` live outside `build/` on purpose: `rm -rf build/tmp`
then costs minutes instead of hours.

## 5. First image

    cd ~/yoctotst && source setup-env.sh
    bitbake dk1-image-dev

Two to four hours and about 50 GB the first time. Validate before committing to
that: `bitbake -p` parses everything, `bitbake -e <target>` shows a variable and
which file set it, `bitbake -n <target>` resolves the whole task graph without
compiling. All three are minutes and catch configuration errors cheaply.

## 6. Flash and boot

    cd build/tmp/deploy/images/stm32mp157d-dk1
    lsblk -o NAME,SIZE,TYPE,RM,MODEL        # find the card, check twice
    sudo bmaptool copy dk1-image-dev-stm32mp157d-dk1.rootfs.wic /dev/sdX

`sdX` is the whole device. `bmaptool` writes only used blocks, so an 811 MB
image takes seconds.

Worth verifying before powering up, because the boot ROM finds the bootloader by
**GPT partition name** and silently does nothing if it is wrong:

    lsblk -o NAME,SIZE,PARTLABEL /dev/sdX

Expect `fsbl1 fsbl2 metadata1 metadata2 fip-a fip-b u-boot-env bootfs rootfs`.
`fsbl1` should start with the ASCII `STM2` header and `fip-a` with `0xAA640001`.

Then: card in the slot underneath, both boot DIP switches **ON**, USB-C power in,
ST-LINK cable to the host, and

    picocom -b 115200 /dev/ttyACM0

Press RESET. You should get TF-A, OP-TEE, U-Boot, a 2-second extlinux menu, then
a login prompt. Log in as `root` with no password -- that is `debug-tweaks` in
`local.conf`, and it should come out of anything that leaves your desk.

### The extlinux menu decides who owns I2C5

    1:  OpenSTLinux                    i2c5 off, M4 peripherals unassigned
    2:  stm32mp157d-dk1-a7-examples    i2c5 -> Linux, as /dev/i2c-1
    3:  stm32mp157d-dk1-m4-examples    i2c5 -> the M4

All three DTBs are already built into the image, so switching is a keypress at
boot, not a rebuild. Entry 1 is the default after two seconds. Check what you
got with `cat /proc/device-tree/model`.

## 7. The gamepad driver

`layers/meta-dk1/recipes-kernel/gamesir-k1/` builds an out-of-tree
`hid_driver` for the pad (USB `3537:1082`). Protocol in
[gamesir-k1-protocol.md](gamesir-k1-protocol.md).

Despite calling itself "Controller for Xbox" it does **not** enumerate as
XInput, which would be a vendor-specific interface with no HID descriptor. Both
its interfaces are ordinary HID, so `hid-generic` claims it and a driver has to
outrank that with its own `id_table`.

Two things the descriptor says that are not true, both found only by pressing
buttons and watching `evtest`:

- It declares 15 buttons. Thirteen exist; numbers 3 and 6 never fire.
- X and Y are not where the obvious mapping puts them. `input-event-codes.h`
  aliases `BTN_X` to `BTN_NORTH` and `BTN_Y` to `BTN_WEST`, which is a SNES-era
  layout. On an Xbox pad X sits west and Y sits north.
  `Documentation/input/gamepad.rst` says report by position, not by label.

Build it against your host kernel first and check the mapping there -- it is a
much faster loop than reflashing a card:

    sudo apt install linux-headers-$(uname -r)
    cd layers/meta-dk1/recipes-kernel/gamesir-k1/files
    make KERNEL_SRC=/lib/modules/$(uname -r)/build
    sudo insmod gamesir-k1.ko

`hid-generic` already holds the device, so replug the pad or move it over:

    echo -n '0003:3537:1082.0003' | sudo tee /sys/bus/hid/drivers/hid-generic/unbind
    echo -n '0003:3537:1082.0003' | sudo tee /sys/bus/hid/drivers/gamesir-k1/bind

## 8. The display

Wiring, pins and the verified init sequence are in
[oled-i2c5.md](oled-i2c5.md). The short version: the Arduino header's D14/D15
are **I2C5**, PA12 and PA11 on AF4, and the panel answers at `0x3C`.

Prove the wiring from Linux before writing any firmware. Boot entry 2, then
`i2cdetect -y 1`, then push an SSD1306 init and a full-screen fill with
`i2ctransfer` straight from the shell. That tells you the panel works, its
address, and whether it is really an SSD1306 rather than an SH1106 -- all
without a line of M4 code in the way. The commands are in that document.

Note the two address views of one peripheral, which will waste an hour if you
grep for the wrong one: Linux sees I2C5 at `0x40015000`, the M4 node is at
`0x4c006000`.

## 9. M4 firmware

`layers/meta-dk1/recipes-extended/stickman-m4/` builds the Cortex-M4 firmware:
it scans I2C5, self-tests whatever display answered, then runs a game that Linux
steers over rpmsg. It does not use OpenAMP -- the rpmsg layer and vrings are
implemented directly, which is a great deal less machinery.

    bitbake stickman-m4

`GAME ?= "stickman"` in the recipe; `snake` is the other option.

Boot entry **3** so the M4 owns I2C5, then:

    echo i2c_screen_cm4.elf > /sys/class/remoteproc/remoteproc0/firmware
    echo start > /sys/class/remoteproc/remoteproc0/state
    cat /sys/class/remoteproc/remoteproc0/state        # running

The firmware logs to `/sys/kernel/debug/remoteproc/remoteproc0/trace0`, which is
the only console this core has. `mount -t debugfs none /sys/kernel/debug` first
if it is not mounted.

Full workflow, including the memory map and pushing a rebuilt ELF over the
serial console with zmodem, is in [m4-workflow.md](m4-workflow.md).

### If you build ST's OpenAMP examples instead

`m4projects-stm32mp1` builds them, and two things bite:

`M4_BOARDS` has to be `STM32MP157C-DK2` even on a DK1. STM32CubeMP1 ships
project trees only for the DK2 and EV1; they are per-board, not per-part, and
both discovery kits are the same MB1272 board, which is what TF-A prints at
boot. Name the DK1 and the recipe matches no directory and builds nothing at
all, with no error.

If you write your own firmware against their OpenAMP, `METAL_MAX_DEVICE_REGIONS`
must be defined as 2. libmetal's generic backend defaults it to 1, but
`openamp.c` declares `shm_device` with two regions, so `metal_device_register`
returns `-EINVAL`, `MX_OPENAMP_Init` fails, and no channel is ever announced --
with nothing logged, because the trace buffer lives behind the OpenAMP that just
failed to start. ST carry it as a compiler define in the CubeIDE `.cproject`.

## 10. The bridge

`layers/meta-dk1/recipes-extended/pad-bridge/` installs `pad-bridge`, which
reads the pad through evdev and writes single-letter commands to the rpmsg tty.

    pad-bridge -v &

`-v` echoes each letter as it is sent, which immediately separates a pad problem
from an rpmsg problem.

It needs `python3-fcntl`, which is its own package in Yocto. The standard
library is split up, so `python3-core` is not enough and the only symptom is
`ModuleNotFoundError: No module named 'fcntl'` at startup.

## Things that cost time

- The USB-C supply. Twice.
- Only one process may hold `/dev/ttyACM0`. Two writers on that UART produce
  interleaved output, dropped characters and a getty respawn storm that looks
  exactly like a dead board. Check with
  `fuser -v /dev/ttyACM0` before blaming the hardware.
- An idle Linux console emits nothing until you type. Zero bytes from a passive
  read is the normal state at a login prompt, not evidence of a problem.
- Stray bytes written to the console get buffered and satisfy U-Boot's "Hit any
  key to stop autoboot" on the next reset, which then looks like a boot failure.
- busybox has no `timeout`, and its `head` needs `-n 8` rather than `-8`.
- `/boot` is not mounted at runtime -- the wic uses `--no-fstab-update`, so mount
  the bootfs partition explicitly to swap a kernel or DTB.
- Yocto 5.0 has no `UNPACKDIR`. `file://` sources land straight in `${WORKDIR}`
  and `S = "${WORKDIR}"` is correct. Newer documentation says otherwise.
- A Cortex-M4 ELF needs `INHIBIT_PACKAGE_STRIP` and
  `INSANE_SKIP += "arch ldflags file-rdeps staticdev"`, or Yocto's QA treats it
  as a broken A7 binary. Strip it yourself with `arm-none-eabi-strip` for
  deployment: `-g3` across HAL and OpenAMP is 2.5 MB, which is four minutes over
  a 115200 line, against 34 KB stripped.
