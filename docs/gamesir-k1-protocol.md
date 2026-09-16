# GameSir-K1 USB protocol

Decoded from the HID report descriptors on 2026-09-16.

    idVendor   0x3537
    idProduct  0x1082
    iProduct   "GameSir-K1 Controller for Xbox"
    bcdUSB     1.11
    2 interfaces, both HID class 3 / subclass 0 / protocol 0

Despite the name it is NOT in XInput mode. XInput would be a vendor-specific
interface (class 0xFF, subclass 0x5D, protocol 0x01) with no HID descriptor,
like an Xbox 360 pad driven by xpad.c. Both interfaces here are real HID, which
is why hid-generic binds and spawns four input devices.

## Interface 0: gamepad (148-byte descriptor)

    EP 0x82 IN   interrupt, 64 bytes, bInterval 1   <- input reports
    EP 0x02 OUT  interrupt, 64 bytes, bInterval 10  <- LED blink timing

Two application collections: Game Pad (report ID 5) and Consumer (report ID 2).

### Input report ID 5, 10 bytes

    byte  bits   field                      range
    ----------------------------------------------------------------
    0     8      report ID = 0x05
    1-2   15     buttons 1..15              0/1 each, LSB = button 1
          1      padding (constant)

          Buttons 3 and 6 are declared but never fire. The other 13, checked
          against evtest on 2026-09-16:

              1  A        7  LB      11  Back
              2  B        8  RB      12  Start
              4  X        9  LT      13  Guide
              5  Y       10  RT      14  L3
                                     15  R3

          Report by position, not label: this is an Xbox layout, so X is
          BTN_WEST and Y is BTN_NORTH. input-event-codes.h aliases BTN_X to
          BTN_NORTH and BTN_Y to BTN_WEST, which is a SNES-era leftover and
          the opposite of what this pad needs.
    3     4      hat switch                 0..7 clockwise from north,
                                            8 = centred (null state)
          4      padding (constant)
    4     8      X   (left stick X)         0..255, centre 128
    5     8      Y   (left stick Y)         0..255, centre 128
    6     8      Z   (right stick X)        0..255, centre 128
    7     8      Rz  (right stick Y)        0..255, centre 128
    8     8      Brake       (L2 trigger)   0..255
    9     8      Accelerator (R2 trigger)   0..255

Axes are 8-bit unsigned with no sign bit, so centre is 128 and you subtract
that yourself if you want a signed range. All four sticks and both triggers
were seen covering the full 0..255 under evtest, and the hat resolved to
-1/0/+1 on both axes. The hat encodes its neutral position
as 8 rather than as a separate bit, which is what "Null State" in the
descriptor means -- decode it before mapping to ABS_HAT0X/ABS_HAT0Y.

### Output report: LED blink timing

Four single-byte output items on the LED usage page: slow blink on/off time,
fast blink on/off time. Not needed to read input; useful later if you want the
driver to drive the player-indicator LEDs.

## Interface 1: keyboard, mouse, consumer (214-byte descriptor)

    EP 0x84 IN   interrupt, 64 bytes, bInterval 1
    EP 0x04 OUT  interrupt, 64 bytes, bInterval 5

    report ID 3    boot-style keyboard: 8 modifier bits, 1 reserved byte,
                   6-byte key array; output report for LEDs
    report ID 2    consumer control, 2 x 16-bit usage codes
    report ID 9    mouse: 5 buttons, 16-bit X/Y, wheel
    report ID 0x10 vendor page 0xFFF0, 63-byte input
    report ID 0x12 vendor page 0xFFF0, 63-byte input
    report ID 0x0F vendor page 0xFFF0, 63-byte output

This interface is not interesting for the gamepad driver. Leave it to
hid-generic so the pad's media keys keep working.

## What hid-generic currently produces

    event22 + js0   "GameSir-K1 Controller for Xbox"        EV=1b (SYN/KEY/ABS/MSC)
    event23         "... Consumer Control"
    event24         "... Keyboard"
    event25         "... Mouse"

The driver replaces the first of these.
