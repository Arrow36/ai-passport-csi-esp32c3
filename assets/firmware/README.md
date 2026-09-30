**English** · [简体中文](README.zh_CN.md)

# CSI Scope 0.1 firmware

Target: FoloToy AI Passport / ESP32-C3 / 8 MB Flash / ESP-IDF 5.5.3.

Download [CSI-Scope-full.bin](CSI-Scope-full.bin) using GitHub's raw/download button. It is a complete merged image; flash at `0x0`. It replaces the original application and may reset Wi-Fi and other settings. A full-chip erase is not required.

```bash
python -m esptool --chip esp32c3 --port <PORT> --baud 460800 write_flash 0x0 CSI-Scope-full.bin
```

The command uses esptool 4.x syntax from the original ESP-IDF 5.5.3 environment. Replace `<PORT>` with the device serial port.

SHA-256: `052047148b311e6b7852b7019d9711ff66e5b4d467cdc661aaf987f01bc99bf1`.
All distributed files are listed in [SHA256SUMS.txt](SHA256SUMS.txt). Individual application, bootloader, partition-table images and `flash_args` are included for segmented flashing.

## Validation scope

- Build: PASS in the original complete gate; firmware layout, merged image, and matching ELF verified again before publication.
- Host tests: PASS in the original gate; 107 Python tests, with 21 skipped due to Windows symlink permissions.
- Device tests: PASS for flashing, device-side hash verification, startup, provisioning, and initial CSI reception. Short observation measured 29–61 valid frames/second; this is not a guaranteed rate.
- Unverified: completed calibration and static/walking score comparisons, all buttons and Chinese glyphs, incorrect-password recovery, reboot/reconnection, and long-term stability.

The bundled binary is the previously verified build, whose embedded version is `0b9e4c8-dirty`. Publication adds documentation and packages existing artifacts; it does not rebuild the firmware. Source changes are based on upstream commit `0b9e4c81ee4421c0bac39ca3561d65a8285acd4a`. No production sensitivity or detection accuracy is claimed.

Full source is in this repository. See the [project README](../../README.md) for provisioning, controls, and limitations.