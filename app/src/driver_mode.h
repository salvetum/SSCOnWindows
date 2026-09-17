/*
 * Dongle driver-mode detection.
 *
 * The UB500 (and friends) works either as a WinUSB device (streaming via
 * BTstack/EXTERNAL HCI) or as a normal Windows Bluetooth adapter (BTHUSB
 * driver). The two modes are mutually exclusive, so the app must detect and
 * clearly report which one is active before it can explain why Windows
 * Bluetooth is missing / present.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef DRIVER_MODE_H
#define DRIVER_MODE_H

#include <string>

enum class DongleDriverMode {
    NotPresent,   /* no device with the requested VID:PID is visible */
    WinUsbStream, /* WinUSB driver active → streaming mode (BTstack) */
    BthUsb,       /* BTHUSB driver active → normal Windows Bluetooth */
    Other,        /* device found, unknown driver service              */
};

struct DongleDriverStatus {
    DongleDriverMode mode = DongleDriverMode::NotPresent;
    std::string service;   /* e.g. "WinUSB", "BTHUSB"                 */
    std::string device_name; /* friendly description if available      */
    std::string instance_id; /* PnP instance path of the devnode       */
};

/*
 * Find the first present device node whose hardware ID contains VID/PID
 * and read its driver service name.  Typical results:
 *   "WinUSB"     → WinUsbStream
 *   "BTHUSB"     → BthUsb
 *   anything else→ Other
 * No matching device → NotPresent.
 */
DongleDriverStatus detect_dongle_driver(uint16_t vid, uint16_t pid);

#endif /* DRIVER_MODE_H */