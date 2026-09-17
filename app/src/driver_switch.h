/*
 * Dongle driver automation (in-app WinUSB <-> BTHUSB toggle).
 *
 * The UB500 (and friends) either streams over BTstack (WinUSB function
 * driver) or acts as a normal Windows Bluetooth adapter (BTHUSB driver).
 * Switching today means firing up Zadig or Device Manager manually; this
 * module automates it with inbox tools only (no Zadig / wdi-simple / libwdi):
 *
 *   Enable WinUSB  (= "Enable Streaming"):
 *     1. Remove any WinUSB-flavored INF packages that match the VID:PID
 *        (keeps the driver store clean; never touches vendor BTHUSB INFs).
 *     2. Generate a minimal WinUSB INF (Include/Needs winusb.inf — the
 *        Microsoft-signed system service) plus a SHA-256 catalog signed by
 *        a persistent self-signed code-signing cert (created/trusted once
 *        via UAC). Modern Windows rejects unsigned catalog-less INFs, so
 *        the catalog is mandatory. Install via elevated
 *        "pnputil /add-driver ... /install /force".
 *     3. Remove + rescan the devnode so Setup re-evaluates the store and
 *        picks the freshly-added WinUSB package (newest driver wins).
 *
 *   Disable WinUSB (= "Restore Windows Bluetooth"):
 *     1. Find every WinUSB-flavored INF in the driver store (oemNN.inf
 *        files that reference winusb.inf AND carry our VID:PID).
 *     2. Delete them via elevated "pnputil /delete-driver ... /uninstall".
 *     3. Remove + rescan the devnode, letting the vendor/inbox BTHUSB
 *        driver (e.g. Realtek's rtkfilter package) take over again.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef DRIVER_SWITCH_H
#define DRIVER_SWITCH_H

#include <cstdint>
#include <string>
#include <vector>

enum class DongleSwitchResult {
    Ok,                 /* switch applied; caller should re-detect  */
    NotPresent,         /* no device with the requested VID:PID     */
    NoChange,           /* already in the requested mode            */
    InfWriteError,      /* could not write the generated INF        */
    ElevationCancelled, /* user declined the UAC prompt             */
    PnPError,           /* pnputil reported a non-zero exit code    */
};

struct DongleSwitchReport {
    DongleSwitchResult result;
    std::string message;
};

/*
 * Switch the dongle's function driver.
 *   enable_winusb == true  -> install WinUSB (BTstack streaming mode)
 *   enable_winusb == false -> restore BTHUSB (normal Windows Bluetooth)
 */
DongleSwitchReport set_dongle_winusb(uint16_t vid, uint16_t pid, bool enable_winusb);

#endif /* DRIVER_SWITCH_H */