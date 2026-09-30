[简体中文](csi-scope.zh_CN.md)

# CSI Scope 0.1

An experimental CSI amplitude and relative disturbance viewer for the ESP32-C3
FoloToy AI Passport. Based on upstream `0b9e4c81ee4421c0bac39ca3561d65a8285acd4a`,
on `feature/csi-scope`, using ESP-IDF 5.5.3 and the pinned BSP dependencies.
The application has its own display; the original hardware-test menu is not used.

## Setup and controls

1. After an authorized flash, power on and join the `CSI-Passport-XXXX` Wi-Fi
   network from a phone. Use the random password shown on the device screen.
2. Keep the connection even though this network has no Internet. Manually open
   `http://192.168.4.1`, enter a 2.4 GHz router SSID/password, and submit.
3. Credentials are stored only on the device and only after successful connection
   and DHCP. The previous saved pair survives a failed attempt. The setup access
   point and HTTP server shut down after success. No cloud service is involved.
4. Fix both the router and device in place. Keep the scene still during the
   initial calibration (at least eight seconds and 100 qualifying samples).
5. Walk across the radio path and compare the trace and score against an empty room.

| Button | Action |
| --- | --- |
| UP | Switch between a roughly ten-second amplitude trace and subcarrier amplitudes |
| DOWN | Hold/resume the displayed trace; collection and score remain live |
| OK | Restart background calibration |
| Hold OK | Open Wi-Fi setup; sampling pauses |
| OK during setup | Return to sampling if the router is still connected |
| Hold DOWN during setup | Clear this application's saved Wi-Fi pair |

Device labels and the phone form are in Simplified Chinese. The device uses
verified 12/14 px [Chinese font subsets](../assets/fonts/README.md).
The battery field degrades to `USB` when the gauge is unavailable; this does not
measure charging state. The three counters mean accepted frames, queue overflow,
and unsupported/invalid/clipped data. Packet rate is measured, not guaranteed.

## Measurement meaning and limits

The device disables Wi-Fi power saving and pings the local gateway with a requested
10 ms interval. Actual sample rate depends on the router and link. It collects
20 MHz OFDM/HT LLTF CSI only from the associated router's BSSID. The callback
copies 128 bytes into a bounded queue; signal processing runs outside the Wi-Fi
task. The 42 selected subcarriers exclude DC, guard regions and hardware-invalid
first words. BLE and audio are unused to preserve RAM and radio time.

The trace is mean raw CSI magnitude in arbitrary units, not RSSI, distance, or
calibrated field strength. The spectrum shows negative then positive selected
subcarriers, omitting the gap near DC. The score uses RMS-normalized amplitude
shape changes relative to a slow reference, then a calibrated noise floor.
Uniform gain changes are suppressed, but AGC, interference, people, pets, fans,
moving the device, and changed furniture can all affect it. The 0–100 score is a
heuristic visualization, not a probability of occupancy or a validated detector.

At least 1.5 seconds without accepted CSI shows missing data instead of zero
disturbance. A resumed stream relearns the background. If RX stays at zero,
check that the gateway answers Ping and the AP supports 20 MHz OFDM/802.11n;
SKIP helps distinguish received but incompatible frames. Range, sensitivity,
packet rate and battery life require device measurements.

## Validation and flashing

Run `tools/validate.sh`. `tests/test_csi_model.c` covers magnitude extraction,
gain normalization, changing multipath shape, calibration, missing data, time
wrap and credential limits. Host tests do not establish real RF behavior.

The default 8 MB partition layout is unchanged. Flash only a verified merged
`full.bin` at `0x0`, after explicit authorization. Its padding overwrites the
NVS/PHY gap and can reset existing settings; it replaces the installed application.
Do not erase the whole chip as a prerequisite. Firmware and matching ELF/MAP
artifacts are retained together by `tools/archive_firmware.py`.

On native Windows the host gate needs Git Bash, a real Python 3 executable,
MinGW GCC and a Windows actionlint supplied via `ACTIONLINT_BIN`. The gate disables
unused-function unwind tables for MinGW runtime tests; repository diagnostic
paths use POSIX separators on all systems. A short path to compiler headers may
be needed when a long workspace path exceeds Windows toolchain limits.

Pending device acceptance: startup and layout, all buttons, phone provisioning,
wrong-password recovery, reboot/reconnect, raw CSI rate, calibration, movement
response, trace hold, no-data indication, repeated setup and memory stability.
