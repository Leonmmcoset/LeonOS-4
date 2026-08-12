/*
 * LeonOS USB bootstrap interface: declares HID keyboard and mouse support.
 * Provides initialization and polling hooks for early USB input handling.
 */
#ifndef NTCLKS_USB_H
#define NTCLKS_USB_H

/* Bootstrap USB host support for standard HID keyboard and mouse devices. */
void usb_init(void);
void usb_poll(void);

#endif
