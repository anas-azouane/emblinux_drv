# OLED on I2C5

SSD1306, 128x64, I2C address 0x3C. Wired to the Arduino header:

    VCC  ->  3V3   CN16 pin 4
    GND  ->  GND   CN16 pin 6
    SDA  ->  D14   CN13 pin 9
    SCL  ->  D15   CN13 pin 10

Those land on I2C5: PA11 = I2C5_SCL, PA12 = I2C5_SDA, both AF4.

## Who owns the bus

I2C5 is `status = "disabled"` in the stock `stm32mp157d-dk1.dts`, so neither
core gets it by default. Two of ST's device trees change that, and both are
already built into our image:

    stm32mp157d-dk1-a7-examples.dtb   &i2c5      okay  -> Linux, /dev/i2c-1
    stm32mp157d-dk1-m4-examples.dtb   &m4_i2c5   okay  -> M4

Pick one from the extlinux menu at boot; no rebuild needed:

    Select the boot mode
    1:  OpenSTLinux                    (stock, i2c5 off)
    2:  stm32mp157d-dk1-a7-examples    (i2c5 -> Linux)
    3:  stm32mp157d-dk1-m4-examples    (i2c5 -> M4)

The timeout is 2 seconds, then it takes entry 1.

Note the two address views of the same peripheral. Linux sees I2C5 at
`0x40015000` (`i2c5: i2c@40015000` in stm32mp151.dtsi); the M4 node
`m4_i2c5` is at `0x4c006000`. Grepping for the wrong one makes it look like
the bus is missing.

## Init sequence

Verified from userspace with i2ctransfer before any firmware existed. Each
command goes as its own transaction with control byte 0x00:

    ae          display off
    20 00       horizontal addressing mode
    b0          page start
    c8          COM scan direction, reversed
    00 10       column low / high nibble
    40          display start line 0
    81 7f       contrast
    a1          segment remap
    a6          normal (not inverted)
    a8 3f       multiplex ratio, 64 rows
    a4          follow RAM, not all-on
    d3 00       display offset
    d5 80       clock divide / oscillator
    d9 f1       pre-charge period
    da 12       COM pins hardware config
    db 40       VCOMH deselect
    8d 14       charge pump on
    af          display on

Then set the window and stream 1024 bytes with control byte 0x40:

    00 21 00 7f     column range 0..127
    00 22 00 07     page range 0..7

Horizontal addressing auto-advances, so the whole framebuffer can go out
without touching the address pointer again. This is what makes the M4 flush
cheap: one 1025-byte I2C write per frame.

## Reproducing the test from the shell

Boot entry 2, then:

    for c in ae 20 00 b0 c8 00 10 40 81 7f a1 a6 a8 3f a4 d3 00 \
             d5 80 d9 f1 da 12 db 40 8d 14 af; do
        i2ctransfer -y 1 w2@0x3c 0x00 0x$c
    done
    i2ctransfer -y 1 w7@0x3c 0x00 0x21 0x00 0x7f 0x22 0x00 0x07
    D=$(awk 'BEGIN{for(i=0;i<32;i++)printf "0xff "}')
    i=0; while [ $i -lt 32 ]; do
        i2ctransfer -y 1 w33@0x3c 0x40 $D; i=$((i+1))
    done

All pixels lit, edge to edge, which is what rules out the SH1106 (132-column
part, 2-pixel offset) and the 128x32 panel.
