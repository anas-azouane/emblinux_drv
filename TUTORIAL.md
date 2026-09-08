# Yocto on the STM32MP157D-DK1 — a hands-on start

Workspace: `/home/debext/yoctotst`. Everything below is meant to be typed.

```
yoctotst/
├── layers/
│   ├── poky/               Yocto core: bitbake + OE-Core + the "poky" distro
│   ├── meta-openembedded/  Extra recipes (meta-oe, meta-python)
│   ├── meta-st-stm32mp/    ST's BSP: TF-A, OP-TEE, U-Boot, kernel
│   └── meta-dk1/           ← yours; the DK1 machine lives here
├── build/conf/             local.conf + bblayers.conf
├── downloads/              fetched source tarballs (shared, keep it)
├── sstate-cache/           build cache (shared, keep it)
└── setup-env.sh
```

All layers are on the **scarthgap** branch (Yocto 5.0 LTS, supported to 2028).
Pinned commits at setup time: poky `b2c16f1`, meta-openembedded `b5874ea`,
meta-st-stm32mp `99e728c`.

---

## Part 0 — Host setup (once)

Debian 12 is a supported Yocto host. You are missing a few packages:

```bash
sudo apt update
sudo apt install -y gawk wget git diffstat unzip texinfo gcc build-essential \
  chrpath socat cpio python3 python3-pip python3-pexpect xz-utils debianutils \
  iputils-ping python3-git python3-jinja2 python3-subunit zstd liblz4-tool \
  file locales libacl1 bmap-tools
```

Yocto refuses to build under a non-UTF-8 locale. Yours is already
`en_US.UTF-8`, so nothing to do.

Never run any of this as root, and never `sudo bitbake`.

---

## Part 1 — The five concepts

Learn these five and the rest is detail.

| Thing | What it is |
|---|---|
| **Recipe** (`.bb`) | How to build *one* piece of software: where to fetch it, how to configure/compile/install, what packages come out. |
| **Layer** (`meta-*`) | A directory of recipes + config with a `conf/layer.conf`. Layers stack; higher priority wins. |
| **Machine** | Hardware description: CPU tune, bootloader, device tree, kernel. Ours: `stm32mp157d-dk1`. |
| **Distro** | Policy that spans all machines: init system, libc, global features. Ours: `poky`. |
| **Image** | A recipe whose output is a filesystem. Ours: `core-image-minimal`. |

The equation to keep in your head:

```
MACHINE (hardware) + DISTRO (policy) + IMAGE (package set) = your build
```

BitBake reads every recipe in every layer, builds a dependency graph, and
executes *tasks* (`do_fetch`, `do_configure`, `do_compile`, `do_install`,
`do_package`, ...). Each task's inputs are hashed; if the hash matches
something in `sstate-cache/`, the task is skipped and the result restored.
That is why the first build takes hours and the second takes minutes.

---

## Part 2 — Enter the environment

```bash
cd /home/debext/yoctotst
source setup-env.sh
```

You are now in `build/` with `bitbake` on your PATH. **You must re-run this in
every new shell.** Look around:

```bash
bitbake-layers show-layers          # the 6 layers and their priorities
bitbake-layers show-recipes | less  # everything buildable
bitbake-layers show-recipes "*u-boot*"
```

Sanity-check the config before committing to a long build:

```bash
bitbake -p                          # parse every recipe, build the cache
bitbake -e core-image-minimal | grep -E "^MACHINE=|^DISTRO=|^STM32MP_DT_FILES_SDCARD="
```

`bitbake -e` dumps the fully expanded environment for a recipe — including a
comment history showing *which file set each variable*. It is the single most
useful debugging command in Yocto. Try:

```bash
bitbake -e virtual/kernel | grep -B5 "^PV="
```

---

## Part 3 — The first build

```bash
bitbake core-image-minimal
```

Expect **2–4 hours** and ~50 GB the first time. It builds a whole
cross-toolchain from scratch, then TF-A, OP-TEE, U-Boot, the kernel, and a
small userspace. Leave it running.

If it dies, the error names a log file — read that, not the console spew:

```bash
less build/tmp/work/*/<recipe>/*/temp/log.do_compile
```

When it finishes:

```bash
ls -lh build/tmp/deploy/images/stm32mp157d-dk1/
```

You should see `core-image-minimal-stm32mp157d-dk1.rootfs.wic`, plus
`arm-trusted-firmware/`, `fip/`, `uImage`/`zImage` and `stm32mp157d-dk1.dtb`.

---

## Part 4 — Flash and boot

Find your SD card carefully — this overwrites the whole device:

```bash
lsblk -o NAME,SIZE,TYPE,MOUNTPOINT,MODEL
```

Then, with `sdX` replaced by the real card (**not** a partition, no digit):

```bash
cd build/tmp/deploy/images/stm32mp157d-dk1
sudo umount /dev/sdX* 2>/dev/null
sudo bmaptool copy core-image-minimal-stm32mp157d-dk1.rootfs.wic /dev/sdX
sync
```

`bmaptool` reads the `.bmap` file and only writes the used blocks — much
faster than `dd`. The `dd` equivalent, if you prefer:
`sudo dd if=...wic of=/dev/sdX bs=8M conv=fsync status=progress`

On the board:

1. Card into the microSD slot on the underside.
2. Both boot DIP switches to **ON** (SD-card boot; both OFF is USB/DFU
   recovery). Check the silkscreen next to the switches.
3. USB-C **CN6 (ST-LINK)** to your PC — this is power *and* the serial console.
4. `sudo picocom -b 115200 /dev/ttyACM0` (or `screen /dev/ttyACM0 115200`).
   Add yourself to `dialout` to skip the sudo: `sudo usermod -aG dialout $USER`.
5. Press the black **RESET** button.

You should see TF-A → OP-TEE → U-Boot → kernel, then a login prompt. Log in as
`root` with no password (that's `debug-tweaks` from `local.conf`).

**If it hangs at the very first line**, the boot switches or the FSBL
partition are wrong. **If TF-A and U-Boot run but the kernel panics**, it's a
device-tree or rootfs problem — much easier to debug, and U-Boot's console is
right there.

---

## Part 5 — Write a recipe

This is the exercise that makes Yocto click. Build a C program into the image.

```bash
cd /home/debext/yoctotst/layers/meta-dk1
mkdir -p recipes-example/hello/files
```

`recipes-example/hello/files/hello.c`:

```c
#include <stdio.h>
int main(void) { printf("hello from my own Yocto recipe\n"); return 0; }
```

`recipes-example/hello/hello_1.0.bb`:

```bitbake
SUMMARY = "Smallest possible custom recipe"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/meta/files/common-licenses/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://hello.c"

# Where do_compile runs. ${WORKDIR} is this recipe's private build dir,
# and file:// sources are unpacked straight into it on Yocto 5.0.
# (Newer releases add UNPACKDIR and default S to ${WORKDIR}/sources --
#  scarthgap has no UNPACKDIR, so don't copy that pattern from newer docs.)
S = "${WORKDIR}"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} ${S}/hello.c -o hello
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 hello ${D}${bindir}/hello
}
```

Three variables to internalise: `${S}` is where the source lives, `${B}` is
where it's built (defaults to `${S}`), `${D}` is a staging directory that
*looks like* the target's root filesystem. You install into `${D}`, and Yocto
turns that into packages.

Build just this recipe, then inspect what it produced:

```bash
source setup-env.sh
bitbake hello
ls build/tmp/work/*/hello/1.0/image/usr/bin/     # ${D}
ls build/tmp/deploy/ipk/*/hello*                 # the .ipk package
```

Now put it in the image. Add to `build/conf/local.conf`:

```
IMAGE_INSTALL:append = " hello"
```

```bash
bitbake core-image-minimal
```

Note this rebuild is minutes, not hours — sstate did its job. Reflash, boot,
and run `hello`.

Useful while iterating on a recipe:

```bash
bitbake -c cleansstate hello    # forget everything about it, rebuild clean
bitbake -c devshell hello       # a shell inside the cross environment, in ${S}
bitbake -c listtasks hello      # every task available
bitbake -g core-image-minimal   # dependency graph -> .dot files
```

---

## Part 6 — Change someone else's recipe with a `.bbappend`

You never edit another layer. You append to it. Turn on the kernel's
`CONFIG_DEBUG_FS`, for example, or just look at what's there:

```bash
bitbake -c menuconfig virtual/kernel     # explore the ST kernel config
bitbake -c diffconfig virtual/kernel     # emit a .cfg fragment of your changes
```

Then in `layers/meta-dk1/recipes-kernel/linux/linux-stm32mp_%.bbappend`:

```bitbake
FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"
SRC_URI += "file://my-tweaks.cfg"
```

The `%` is a wildcard on the version, so the append survives ST bumping the
kernel. `FILESEXTRAPATHS` is what lets your layer's `files/` directory be
found. Same pattern works for U-Boot, busybox, anything.

Make your own image recipe instead of hacking `local.conf`, in
`layers/meta-dk1/recipes-core/images/dk1-image.bb`:

```bitbake
SUMMARY = "My DK1 image"
require recipes-core/images/core-image-minimal.bb

IMAGE_INSTALL += "hello openssh"
IMAGE_FEATURES += "ssh-server-openssh"
```

`bitbake dk1-image`.

---

## What to read next

- `bitbake-layers show-recipes`, then go read recipes in `layers/poky/meta/recipes-core/`.
  Reading real recipes teaches faster than any doc.
- Yocto Mega-Manual (match the **scarthgap** version): https://docs.yoctoproject.org/5.0/
- ST wiki for this board: https://wiki.st.com/stm32mpu — for the pinout,
  boot switches, and the full OpenSTLinux distro when you outgrow this setup.
- `devtool modify <recipe>` — the modern workflow for hacking on a recipe's
  source with git, then `devtool finish` to turn your changes into patches.

## Gotchas that will bite you

- Forgetting `source setup-env.sh` in a new shell.
- Editing files under `build/tmp/work/` and wondering why changes vanish —
  that directory is disposable output, not source.
- `rm -rf build/tmp` is safe and often the right answer. Deleting
  `downloads/` or `sstate-cache/` costs you hours.
- Recipe filename *is* the version: `hello_1.0.bb` → `PN=hello`, `PV=1.0`.
- Whitespace matters in `:append`. `IMAGE_INSTALL:append = " hello"` needs the
  leading space; `+=` on an image variable does not.
