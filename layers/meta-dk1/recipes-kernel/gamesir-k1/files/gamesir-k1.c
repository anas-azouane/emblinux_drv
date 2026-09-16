// SPDX-License-Identifier: GPL-2.0-only
/*
 * GameSir-K1 USB gamepad.
 *
 * The pad exposes two HID interfaces. Interface 0 is the gamepad proper and
 * is what we drive. Interface 1 is a keyboard/mouse/consumer composite used
 * for the media keys; hid-generic handles it correctly, so we hand it back.
 */

#include <asm/unaligned.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/usb.h>

#define USB_VENDOR_ID_GAMESIR		0x3537
#define USB_DEVICE_ID_GAMESIR_K1	0x1082

#define GAMESIR_PAD_IFNUM		0
#define GAMESIR_PAD_REPORT_ID		0x05
#define GAMESIR_PAD_REPORT_LEN		10

struct gamesir {
	struct hid_device *hdev;
	struct input_dev *input;
};

/*
 * Buttons 1..15 in descriptor order, mapped the way hid-input maps a generic
 * pad. The pad ships no usage labels, so this ordering is a guess until it is
 * checked against evtest.
 */
static const unsigned int gamesir_btn[] = {
	BTN_SOUTH, BTN_EAST, BTN_C, BTN_NORTH, BTN_WEST,
	BTN_Z, BTN_TL, BTN_TR, BTN_TL2, BTN_TR2,
	BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR,
};

/* Hat runs 0..7 clockwise from north; 8 means centred. */
static const s8 gamesir_hat_x[8] = {  0,  1, 1, 1, 0, -1, -1, -1 };
static const s8 gamesir_hat_y[8] = { -1, -1, 0, 1, 1,  1,  0, -1 };

static int gamesir_open(struct input_dev *input)
{
	struct gamesir *gs = input_get_drvdata(input);

	return hid_hw_open(gs->hdev);
}

static void gamesir_close(struct input_dev *input)
{
	struct gamesir *gs = input_get_drvdata(input);

	hid_hw_close(gs->hdev);
}

static int gamesir_input_setup(struct gamesir *gs)
{
	struct hid_device *hdev = gs->hdev;
	struct input_dev *input;
	int i;

	input = devm_input_allocate_device(&hdev->dev);
	if (!input)
		return -ENOMEM;

	input->name = "GameSir-K1 Gamepad";
	input->phys = hdev->phys;
	input->uniq = hdev->uniq;
	input->id.bustype = hdev->bus;
	input->id.vendor = hdev->vendor;
	input->id.product = hdev->product;
	input->id.version = hdev->version;
	input->dev.parent = &hdev->dev;
	input->open = gamesir_open;
	input->close = gamesir_close;

	input_set_drvdata(input, gs);

	for (i = 0; i < ARRAY_SIZE(gamesir_btn); i++)
		input_set_capability(input, EV_KEY, gamesir_btn[i]);

	/*
	 * The descriptor calls the right stick Z/Rz and the triggers
	 * Brake/Accelerator. Remap onto the ABS_RX/RY + ABS_Z/RZ layout from
	 * Documentation/input/gamepad.rst so userspace sees a normal pad.
	 * Sticks get a flat zone; triggers rest at 0 and must not have one.
	 */
	input_set_abs_params(input, ABS_X, 0, 255, 0, 15);
	input_set_abs_params(input, ABS_Y, 0, 255, 0, 15);
	input_set_abs_params(input, ABS_RX, 0, 255, 0, 15);
	input_set_abs_params(input, ABS_RY, 0, 255, 0, 15);
	input_set_abs_params(input, ABS_Z, 0, 255, 0, 0);
	input_set_abs_params(input, ABS_RZ, 0, 255, 0, 0);
	input_set_abs_params(input, ABS_HAT0X, -1, 1, 0, 0);
	input_set_abs_params(input, ABS_HAT0Y, -1, 1, 0, 0);

	gs->input = input;

	return input_register_device(input);
}

static int gamesir_raw_event(struct hid_device *hdev, struct hid_report *report,
			     u8 *data, int size)
{
	struct gamesir *gs = hid_get_drvdata(hdev);
	u16 buttons;
	u8 hat;
	int i;

	if (!gs || !gs->input)
		return 0;

	if (size < GAMESIR_PAD_REPORT_LEN || data[0] != GAMESIR_PAD_REPORT_ID)
		return 0;

	buttons = get_unaligned_le16(&data[1]);
	for (i = 0; i < ARRAY_SIZE(gamesir_btn); i++)
		input_report_key(gs->input, gamesir_btn[i], buttons & BIT(i));

	hat = data[3] & 0x0f;
	if (hat < ARRAY_SIZE(gamesir_hat_x)) {
		input_report_abs(gs->input, ABS_HAT0X, gamesir_hat_x[hat]);
		input_report_abs(gs->input, ABS_HAT0Y, gamesir_hat_y[hat]);
	} else {
		input_report_abs(gs->input, ABS_HAT0X, 0);
		input_report_abs(gs->input, ABS_HAT0Y, 0);
	}

	input_report_abs(gs->input, ABS_X, data[4]);
	input_report_abs(gs->input, ABS_Y, data[5]);
	input_report_abs(gs->input, ABS_RX, data[6]);
	input_report_abs(gs->input, ABS_RY, data[7]);
	input_report_abs(gs->input, ABS_Z, data[8]);
	input_report_abs(gs->input, ABS_RZ, data[9]);

	input_sync(gs->input);

	return 1;
}

static int gamesir_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct usb_interface *intf;
	struct gamesir *gs;
	int ret;

	if (!hid_is_usb(hdev))
		return -ENODEV;

	ret = hid_parse(hdev);
	if (ret)
		return ret;

	intf = to_usb_interface(hdev->dev.parent);
	if (intf->cur_altsetting->desc.bInterfaceNumber != GAMESIR_PAD_IFNUM)
		return hid_hw_start(hdev, HID_CONNECT_DEFAULT);

	gs = devm_kzalloc(&hdev->dev, sizeof(*gs), GFP_KERNEL);
	if (!gs)
		return -ENOMEM;

	gs->hdev = hdev;
	hid_set_drvdata(hdev, gs);

	/* Without HIDINPUT, hid-input leaves the input device to us. */
	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT & ~HID_CONNECT_HIDINPUT);
	if (ret)
		return ret;

	ret = gamesir_input_setup(gs);
	if (ret) {
		hid_hw_stop(hdev);
		return ret;
	}

	return 0;
}

static void gamesir_remove(struct hid_device *hdev)
{
	hid_hw_stop(hdev);
}

static const struct hid_device_id gamesir_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_GAMESIR, USB_DEVICE_ID_GAMESIR_K1) },
	{ }
};
MODULE_DEVICE_TABLE(hid, gamesir_devices);

static struct hid_driver gamesir_hid_driver = {
	.name		= "gamesir-k1",
	.id_table	= gamesir_devices,
	.probe		= gamesir_probe,
	.remove		= gamesir_remove,
	.raw_event	= gamesir_raw_event,
};
module_hid_driver(gamesir_hid_driver);

MODULE_DESCRIPTION("GameSir-K1 USB gamepad");
MODULE_LICENSE("GPL");
