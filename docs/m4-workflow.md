# Running firmware on the Cortex-M4

Proven end to end on 2026-10-03 with ST's OpenAMP_TTY_echo example.

## Boot the right device tree

The M4 only gets its peripherals under the m4-examples tree. Pick entry 3 at
the extlinux prompt; the timeout is 2 seconds and the default is entry 1.

    Select the boot mode
    1:  OpenSTLinux                    i2c5 off, M4 peripherals unassigned
    2:  stm32mp157d-dk1-a7-examples    i2c5 -> Linux as /dev/i2c-1
    3:  stm32mp157d-dk1-m4-examples    i2c5 -> M4

Check which one you got:

    cat /proc/device-tree/model

Under entry 3 the model string ends "configured to run M4 examples", and
`/dev/i2c-*` drops back to 0,1,2 because I2C5 has left Linux. That missing bus
is how you confirm the handover worked.

## Build

The firmware is built by ST's own recipe, not anything hand-rolled:

    bitbake m4projects-stm32mp1

`layers/meta-dk1/.../m4projects-stm32mp1.bbappend` narrows PROJECTS_LIST to
the two projects we care about. Output lands in
`/usr/local/Cube-M4-examples/<project>/lib/firmware/`.

Note `M4_BOARDS = "STM32MP157C-DK2"` in the machine conf even though this is a
DK1. STM32CubeMP1 ships project trees only for STM32MP157C-DK2 and -EV1; the
projects are per-board and both discovery kits are the same MB1272 board, which
is what TF-A prints at boot. Setting it to STM32MP157D-DK1 matches no directory
and the recipe builds nothing at all, with no error.

## Get it onto the board over UART

There is no ethernet on this setup, so transfers go over the serial console
with zmodem. `lrzsz` is in the image and on the host.

On the board:

    mkdir -p /lib/firmware && cd /lib/firmware
    rz -y

Then from the host, with nothing else holding the port:

    sz -b <firmware>.elf < /dev/ttyACM0 > /dev/ttyACM0

About 11 KB/s, so a 50 KB ELF takes roughly 5 seconds. Always check it
arrived intact -- `md5sum` on both ends.

In picocom the same thing is Ctrl-A Ctrl-S, then type the host path.

## Load it

    echo OpenAMP_TTY_echo.elf > /sys/class/remoteproc/remoteproc0/firmware
    echo start > /sys/class/remoteproc/remoteproc0/state
    cat /sys/class/remoteproc/remoteproc0/state      # running

`echo stop > state` to unload. The firmware name is relative to /lib/firmware.

A successful start logs this:

    rproc-virtio: assigned reserved memory node vdev0buffer@10042000
    virtio_rpmsg_bus virtio0: creating channel rpmsg-tty addr 0x400
    virtio_rpmsg_bus virtio0: rpmsg host is online
    remoteproc remoteproc0: remote processor m4 is now up

and creates /dev/ttyRPMSG0, /dev/ttyRPMSG1, /dev/rpmsg_ctrl0.

## Talk to it

    stty -F /dev/ttyRPMSG0 raw -echo
    (cat /dev/ttyRPMSG0 > /tmp/r.out &)
    printf 'ABC123' > /dev/ttyRPMSG0
    sleep 1; cat /tmp/r.out            # ABC123, echoed by the M4

## Debugging

    /sys/kernel/debug/remoteproc/remoteproc0/trace0     firmware log buffer
    /sys/kernel/debug/remoteproc/remoteproc0/crash
    /sys/kernel/debug/remoteproc/remoteproc0/carveout_memories

Mount debugfs first if it is not already: `mount -t debugfs none /sys/kernel/debug`.
Busybox head needs `-n 8`, not `-8`, which is easy to waste time on.

## Memory map

From the linked ELF and the carveouts remoteproc reports:

    0x00000000   vector table (RETRAM, M4 view)
    0x10000000   .text
    0x10020000   .data
    0x10020120   .resource_table   0x8c bytes
    0x10040000   vdev0vring0       4K
    0x10041000   vdev0vring1       4K
    0x10042000   vdev0buffer       16K
    0x10048000   mcu-rsc-table     32K

Addresses above are the M4 view. The A7 sees MCU SRAM at 0x30000000 and
RETRAM at 0x38000000, and the same split applies to peripherals: Linux sees
I2C5 at 0x40015000 while the M4 node is 0x4c006000.
