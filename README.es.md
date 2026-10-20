# Linuxbox-build

[English](README.md) | [简体中文](README.zh-CN.md) | **Español** | [Русский](README.ru.md)

Imágenes Debian / Ubuntu para el **ThirdReality LinuxBox HubV3** (Amlogic AXG / A113X), construidas sobre el
[framework de compilación de Armbian](https://github.com/armbian/build) original **sin modificar**.

Todo lo específico de la placa está en `userpatches/`; el árbol de Armbian se clona por versión y nunca se
edita, así que pasar a una nueva versión de Armbian es un cambio de una línea.

## Características

| Componente | Detalles |
|---|---|
| Kernel | Linux **6.6.158** (fijo), parches Armbian `meson64-6.6` + parches LinuxBox |
| U-Boot | **v2023.10** (fijo), FIP generado con [amlogic-boot-tools](https://github.com/thirdreality/amlogic-boot-tools) |
| WiFi / Bluetooth | Amlogic W155S1 (W1): drivers `aml_sdio` / `vlsicomm` / `sdio_bt`, firmware, `aml_hciattach`. Driver WiFi de [CoreELEC/w1-aml](https://github.com/CoreELEC/w1-aml) (v1.8.3.3_20260310-w1) |
| Zigbee / Thread | Grabador BL706 empaquetado como `linuxbox-bl706` (sin conexión, no necesita red) |
| Imagen de grabación | Imagen para Amlogic USB Burning Tool (`*.burn.img.zip`) generada en cada compilación |
| Sistema | `/boot` montado en solo lectura, cgroup v1, NetworkManager (`p2p0` no gestionado) |

## Versiones de Armbian y distribuciones soportadas

| Armbian | Distribuciones |
|---|---|
| v24.11.1 (por defecto) | bookworm, jammy, noble |
| v25.2.1 | bookworm, jammy, noble |
| v25.5.1 | bookworm, noble |
| v25.8.2 | bookworm, noble, trixie |
| v25.11.1 | noble, trixie |
| v26.2.1 | noble, trixie |
| v26.5.1 | noble, resolute, trixie |
| v26.08 (rama) | noble, resolute, trixie |
| main (trunk) | noble, resolute, trixie |

La lista está en [versions.conf](versions.conf). Las entradas `vX.Y.Z` son tags; `vYY.MM` y `main` son ramas
y siguen el HEAD de upstream en el momento de clonar.

## Requisitos

- Host Linux x86_64; se recomienda Debian 12 / Ubuntu 22.04 o superior
- ~20 GB de espacio libre por versión de Armbian
- `git`, `rsync`; Armbian instala el resto de dependencias del host
- Ejecutar como root (compilación nativa) o como usuario del grupo `docker` (Armbian compila en un contenedor)

## Inicio rápido

```bash
git clone git@github.com:thirdreality/Linuxbox-build.git
cd Linuxbox-build

# por defecto: Armbian v24.11.1, Debian bookworm
./build.sh

# otras combinaciones
./build.sh --armbian v25.8.2 --release trixie
./build.sh --armbian v26.08  --release noble

# listar versiones soportadas
./build.sh --help
```

Se pueden pasar opciones adicionales de Armbian después de `--`:

```bash
./build.sh --armbian v24.11.1 --release bookworm -- KERNEL_CONFIGURE=yes
```

## Resultados

```
armbian/<versión>/output/images/
├── armbian_<ver>_Linuxbox-hubv3_<release>_current_6.6.158.img            # imagen de disco
└── armbian_<ver>_Linuxbox-hubv3_<release>_current_6.6.158.burn.img.zip   # imagen para Amlogic USB Burning Tool
armbian/<versión>/output/debs/linuxbox-bl706_<ver>_all.deb                # paquete del grabador BL706
```

## Grabación

Descomprima `*.burn.img.zip` y escríbalo en la eMMC con Amlogic USB Burning Tool con la placa en modo de
descarga USB.

En el primer arranque, inicie sesión por la consola serie (`ttyAML0`, 115200 8N1) como `root` / `1234`;
Armbian pedirá una contraseña nueva.

## En el dispositivo

```bash
# WiFi
nmcli dev wifi connect "<SSID>" password "<contraseña>"

# Bluetooth
hciconfig -a
bluetoothctl --timeout 15 scan on

# BL706 (Zigbee en /dev/ttyAML3, Thread en /dev/ttyAML6)
/usr/lib/firmware/bl706/bl706_func.sh flash zigbee
/usr/lib/firmware/bl706/bl706_func.sh flash thread
```

## Estructura del repositorio

```
build.sh                    punto de entrada: clona Armbian, sincroniza userpatches/, ejecuta compile.sh
versions.conf               versiones de Armbian y sus distribuciones soportadas
userpatches/
├── config/boards/linuxbox-hubv3.conf           definición de la placa
├── config/sources/families/linuxbox.conf       familia: versiones de kernel/u-boot, FIP, extensiones
├── config/kernel/linux-linuxbox-current.config configuración del kernel
├── kernel/linuxbox-6.6/                        parches del kernel (Armbian meson64-6.6 + linuxbox-*)
├── u-boot/linuxbox-v2023.10/board_linuxbox-hubv3/  parches de u-boot
├── bootscripts/boot-linuxbox.cmd               script de arranque
└── extensions/
    ├── linuxbox-hubv3.sh     /boot en solo lectura, cgroup v1, BOARD_NAME
    ├── linuxbox-w1/          firmware, scripts, servicios y código de aml_hciattach para W155S1
    ├── linuxbox-bl706/       paquete del grabador BL706 (bflb_iot, firmware, wheels de Python sin conexión)
    └── linuxbox-burn.sh      conversión a imagen de grabación Amlogic
armbian/<versión>/          checkout de Armbian original (ignorado por git, nunca modificado)
```

## Principios de diseño

- **Cero cambios en Armbian.** `build.sh` se niega a compilar si el checkout de Armbian tiene modificaciones locales.
- **Solo lo que necesita el hardware.** Sin software de aplicación (Home Assistant, MQTT, etc.).
- **Kernel y U-Boot fijos** en todas las versiones de Armbian, para que todas se comporten igual en la placa.

## Licencia

Armbian es GPL-2.0. Los parches de kernel y U-Boot siguen las licencias de sus proyectos (GPL-2.0).
Los firmwares se redistribuyen tal como los proporcionan sus fabricantes.
