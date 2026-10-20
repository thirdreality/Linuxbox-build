# Linuxbox-build

[English](README.md) | [简体中文](README.zh-CN.md) | [Español](README.es.md) | **Русский**

Образы Debian / Ubuntu для **ThirdReality LinuxBox HubV3** (Amlogic AXG / A113X), собираемые поверх
**немодифицированной** системы сборки [Armbian](https://github.com/armbian/build).

Всё, что относится к плате, находится в `userpatches/`; дерево Armbian клонируется для каждой версии и
никогда не изменяется, поэтому переход на новую версию Armbian — это изменение одной строки.

## Возможности

| Компонент | Описание |
|---|---|
| Ядро | Linux **6.6.158** (фиксировано), патчи Armbian `meson64-6.6` + патчи LinuxBox |
| U-Boot | **v2023.10** (фиксировано), FIP собирается с помощью [amlogic-boot-tools](https://github.com/thirdreality/amlogic-boot-tools) |
| WiFi / Bluetooth | Amlogic W155S1 (W1): драйверы `aml_sdio` / `vlsicomm` / `sdio_bt`, прошивки, `aml_hciattach`. Драйвер WiFi из [CoreELEC/w1-aml](https://github.com/CoreELEC/w1-aml) (v1.8.3.3_20260310-w1) |
| Zigbee / Thread | Прошивальщик BL706 в пакете `linuxbox-bl706` (работает без сети) |
| Образ для прошивки | Образ для Amlogic USB Burning Tool (`*.burn.img.zip`) создаётся при каждой сборке |
| Система | `/boot` только для чтения, cgroup v1, NetworkManager (`p2p0` не управляется) |

## Поддерживаемые версии Armbian и дистрибутивы

| Armbian | Дистрибутивы |
|---|---|
| v24.11.1 (по умолчанию) | bookworm, jammy, noble |
| v25.2.1 | bookworm, jammy, noble |
| v25.5.1 | bookworm, noble |
| v25.8.2 | bookworm, noble, trixie |
| v25.11.1 | noble, trixie |
| v26.2.1 | noble, trixie |
| v26.5.1 | noble, resolute, trixie |
| v26.08 (ветка) | noble, resolute, trixie |
| main (trunk) | noble, resolute, trixie |

Список задан в [versions.conf](versions.conf). Записи `vX.Y.Z` — теги; `vYY.MM` и `main` — ветки, берётся
текущий HEAD upstream на момент клонирования.

## Требования

- Хост x86_64 Linux, рекомендуется Debian 12 / Ubuntu 22.04 или новее
- ~20 ГБ свободного места на каждую версию Armbian
- `git`, `rsync`; остальные зависимости Armbian устанавливает сам
- Запуск от root (нативная сборка) или от пользователя из группы `docker` (Armbian собирает в контейнере)

## Быстрый старт

```bash
git clone git@github.com:thirdreality/Linuxbox-build.git
cd Linuxbox-build

# по умолчанию: Armbian v24.11.1, Debian bookworm
./build.sh

# другие комбинации
./build.sh --armbian v25.8.2 --release trixie
./build.sh --armbian v26.08  --release noble

# список поддерживаемых версий
./build.sh --help
```

Дополнительные параметры Armbian передаются после `--`:

```bash
./build.sh --armbian v24.11.1 --release bookworm -- KERNEL_CONFIGURE=yes
```

## Результаты сборки

```
armbian/<версия>/output/images/
├── armbian_<ver>_Linuxbox-hubv3_<release>_current_6.6.158.img            # образ диска
└── armbian_<ver>_Linuxbox-hubv3_<release>_current_6.6.158.burn.img.zip   # образ для Amlogic USB Burning Tool
armbian/<версия>/output/debs/linuxbox-bl706_<ver>_all.deb                 # пакет прошивальщика BL706
```

## Прошивка

Распакуйте `*.burn.img.zip` и запишите его в eMMC с помощью Amlogic USB Burning Tool, переведя плату в
режим USB-загрузки.

При первом запуске войдите через последовательную консоль (`ttyAML0`, 115200 8N1) как `root` / `1234`;
Armbian попросит задать новый пароль.

## На устройстве

```bash
# WiFi
nmcli dev wifi connect "<SSID>" password "<пароль>"

# Bluetooth
hciconfig -a
bluetoothctl --timeout 15 scan on

# BL706 (Zigbee на /dev/ttyAML3, Thread на /dev/ttyAML6)
/usr/lib/firmware/bl706/bl706_func.sh flash zigbee
/usr/lib/firmware/bl706/bl706_func.sh flash thread
```

## Структура репозитория

```
build.sh                    точка входа: клонирует Armbian, синхронизирует userpatches/, запускает compile.sh
versions.conf               версии Armbian и поддерживаемые ими дистрибутивы
userpatches/
├── config/boards/linuxbox-hubv3.conf           описание платы
├── config/sources/families/linuxbox.conf       семейство: версии ядра/u-boot, FIP, расширения
├── config/kernel/linux-linuxbox-current.config конфигурация ядра
├── kernel/linuxbox-6.6/                        патчи ядра (Armbian meson64-6.6 + linuxbox-*)
├── u-boot/linuxbox-v2023.10/board_linuxbox-hubv3/  патчи u-boot
├── bootscripts/boot-linuxbox.cmd               скрипт загрузки
└── extensions/
    ├── linuxbox-hubv3.sh     /boot только для чтения, cgroup v1, BOARD_NAME
    ├── linuxbox-w1/          прошивки, скрипты, службы W155S1 и исходники aml_hciattach
    ├── linuxbox-bl706/       пакет прошивальщика BL706 (bflb_iot, прошивки, офлайн-пакеты Python)
    └── linuxbox-burn.sh      преобразование в образ для прошивки Amlogic
armbian/<версия>/           checkout Armbian (игнорируется git, никогда не изменяется)
```

## Принципы

- **Никаких изменений в Armbian.** `build.sh` отказывается собирать, если в checkout Armbian есть локальные изменения.
- **Только то, что нужно оборудованию.** Без прикладного ПО (Home Assistant, MQTT и т. п.).
- **Фиксированные ядро и U-Boot** во всех версиях Armbian, чтобы плата вела себя одинаково.

## Лицензия

Armbian распространяется под GPL-2.0. Патчи ядра и U-Boot — под лицензиями соответствующих проектов (GPL-2.0).
Прошивки распространяются в том виде, в каком их предоставили производители.
