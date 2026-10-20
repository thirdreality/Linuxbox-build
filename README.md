# Linuxbox-build

**English** | [简体中文](README.zh-CN.md) | [Español](README.es.md) | [Русский](README.ru.md)

Debian / Ubuntu images for the **ThirdReality LinuxBox HubV3** (Amlogic AXG / A113X), built on top of an
**unmodified** upstream [Armbian build framework](https://github.com/armbian/build).

Everything board-specific lives in `userpatches/`; the Armbian tree is cloned per version and never edited,
so moving to a new Armbian release is a one-line change.

## Features

| Component | Details |
|---|---|
| Kernel | Linux **6.6.158** (fixed), Armbian `meson64-6.6` patch set + LinuxBox patches |
| U-Boot | **v2023.10** (fixed), FIP built with [amlogic-boot-tools](https://github.com/thirdreality/amlogic-boot-tools) |
| WiFi / Bluetooth | Amlogic W155S1 (W1): `aml_sdio` / `vlsicomm` / `sdio_bt` drivers, firmware, `aml_hciattach`. WiFi driver from [CoreELEC/w1-aml](https://github.com/CoreELEC/w1-aml) (v1.8.3.3_20260310-w1) |
| Zigbee / Thread | BL706 flasher packaged as `linuxbox-bl706` (offline, no network needed) |
| Burn image | Amlogic USB Burning Tool image (`*.burn.img.zip`) produced automatically with every build |
| System | `/boot` mounted read-only, cgroup v1, NetworkManager (`p2p0` unmanaged) |

## Supported Armbian versions and releases

| Armbian | Releases |
|---|---|
| v24.11.1 (default) | bookworm, jammy, noble |
| v25.2.1 | bookworm, jammy, noble |
| v25.5.1 | bookworm, noble |
| v25.8.2 | bookworm, noble, trixie |
| v25.11.1 | noble, trixie |
| v26.2.1 | noble, trixie |
| v26.5.1 | noble, resolute, trixie |
| v26.08 (branch) | noble, resolute, trixie |
| main (trunk) | noble, resolute, trixie |

The list is defined in [versions.conf](versions.conf). `vX.Y.Z` entries are tags; `vYY.MM` and `main` are
branches and follow upstream HEAD at clone time.

## Requirements

- x86_64 Linux host, Debian 12 / Ubuntu 22.04+ recommended
- ~20 GB free disk space per Armbian version
- `git`, `rsync`; Armbian installs the rest of its host dependencies itself
- Run as root (native build) or as a user in the `docker` group (Armbian runs the build in a container)

## Quick start

```bash
git clone git@github.com:thirdreality/Linuxbox-build.git
cd Linuxbox-build

# default: Armbian v24.11.1, Debian bookworm
./build.sh

# other combinations
./build.sh --armbian v25.8.2 --release trixie
./build.sh --armbian v26.08  --release noble

# list supported versions
./build.sh --help
```

Extra Armbian options can be passed after `--`:

```bash
./build.sh --armbian v24.11.1 --release bookworm -- KERNEL_CONFIGURE=yes
```

## Output

```
armbian/<version>/output/images/
├── armbian_<ver>_Linuxbox-hubv3_<release>_current_6.6.158.img            # raw disk image
└── armbian_<ver>_Linuxbox-hubv3_<release>_current_6.6.158.burn.img.zip   # Amlogic USB Burning Tool image
armbian/<version>/output/debs/linuxbox-bl706_<ver>_all.deb                # BL706 flasher package
```

## Flashing

Unzip `*.burn.img.zip` and write it to the eMMC with the Amlogic USB Burning Tool while the board is in
USB download mode.

On first boot, log in on the serial console (`ttyAML0`, 115200 8N1) as `root` / `1234`; Armbian asks for a
new password.

## On the device

```bash
# WiFi
nmcli dev wifi connect "<SSID>" password "<password>"

# Bluetooth
hciconfig -a
bluetoothctl --timeout 15 scan on

# BL706 (Zigbee on /dev/ttyAML3, Thread on /dev/ttyAML6)
/usr/lib/firmware/bl706/bl706_func.sh flash zigbee
/usr/lib/firmware/bl706/bl706_func.sh flash thread
```

## Repository layout

```
build.sh                    entry point: clone Armbian, sync userpatches/, run compile.sh
versions.conf               Armbian versions and their supported releases
userpatches/
├── config/boards/linuxbox-hubv3.conf           board definition
├── config/sources/families/linuxbox.conf       family: kernel/u-boot versions, FIP, extensions
├── config/kernel/linux-linuxbox-current.config kernel config
├── kernel/linuxbox-6.6/                        kernel patches (Armbian meson64-6.6 + linuxbox-*)
├── u-boot/linuxbox-v2023.10/board_linuxbox-hubv3/  u-boot patches
├── bootscripts/boot-linuxbox.cmd               boot script
└── extensions/
    ├── linuxbox-hubv3.sh     /boot read-only, cgroup v1, BOARD_NAME
    ├── linuxbox-w1/          W155S1 WiFi/BT firmware, scripts, services, aml_hciattach source
    ├── linuxbox-bl706/       BL706 flasher package (bflb_iot, firmware, offline Python wheels)
    └── linuxbox-burn.sh      Amlogic burn image conversion
armbian/<version>/          upstream Armbian checkout (git-ignored, never modified)
```

## Design rules

- **Zero changes to Armbian.** `build.sh` refuses to build if the Armbian checkout has local modifications.
- **Only what the hardware needs.** No application-layer software (Home Assistant, MQTT, etc.).
- **Fixed kernel and U-Boot** across all Armbian versions, so every release behaves the same on the board.

## License

Armbian is GPL-2.0. Kernel and U-Boot patches follow the licenses of the respective projects (GPL-2.0).
Firmware blobs are redistributed as provided by their vendors.
