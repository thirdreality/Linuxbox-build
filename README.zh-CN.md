# Linuxbox-build

[English](README.md) | **简体中文** | [Español](README.es.md) | [Русский](README.ru.md)

为 **ThirdReality LinuxBox HubV3**（Amlogic AXG / A113X）构建 Debian / Ubuntu 镜像，基于**未经修改**的上游
[Armbian 构建框架](https://github.com/armbian/build)。

所有板级内容都放在 `userpatches/` 中；Armbian 源码按版本单独克隆，从不修改，
因此升级到新的 Armbian 版本只需改一行。

## 功能

| 组件 | 说明 |
|---|---|
| 内核 | Linux **6.6.158**（固定），Armbian `meson64-6.6` 补丁集 + LinuxBox 补丁 |
| U-Boot | **v2023.10**（固定），FIP 由 [amlogic-boot-tools](https://github.com/thirdreality/amlogic-boot-tools) 生成 |
| WiFi / 蓝牙 | Amlogic W155S1 (W1)：`aml_sdio` / `vlsicomm` / `sdio_bt` 驱动、固件、`aml_hciattach`。WiFi 驱动来自 [CoreELEC/w1-aml](https://github.com/CoreELEC/w1-aml)（v1.8.3.3_20260310-w1） |
| Zigbee / Thread | BL706 烧录工具，打包为 `linuxbox-bl706`（离线，无需联网） |
| 烧录镜像 | 每次编译自动生成 Amlogic USB Burning Tool 镜像（`*.burn.img.zip`） |
| 系统 | `/boot` 只读挂载、cgroup v1、NetworkManager（不管理 `p2p0`） |

## 支持的 Armbian 版本与发行版

| Armbian | 发行版 |
|---|---|
| v24.11.1（默认） | bookworm, jammy, noble |
| v25.2.1 | bookworm, jammy, noble |
| v25.5.1 | bookworm, noble |
| v25.8.2 | bookworm, noble, trixie |
| v25.11.1 | noble, trixie |
| v26.2.1 | noble, trixie |
| v26.5.1 | noble, resolute, trixie |
| v26.08（分支） | noble, resolute, trixie |
| main（主干） | noble, resolute, trixie |

列表定义在 [versions.conf](versions.conf)。`vX.Y.Z` 为 tag；`vYY.MM` 与 `main` 为分支，克隆时取上游最新提交。

## 环境要求

- x86_64 Linux 主机，推荐 Debian 12 / Ubuntu 22.04 及以上
- 每个 Armbian 版本约需 20 GB 可用磁盘空间
- `git`、`rsync`；其余主机依赖由 Armbian 自动安装
- 以 root 运行（本机编译），或以 `docker` 组用户运行（Armbian 在容器中编译）

## 快速开始

```bash
git clone git@github.com:thirdreality/Linuxbox-build.git
cd Linuxbox-build

# 默认：Armbian v24.11.1，Debian bookworm
./build.sh

# 其他组合
./build.sh --armbian v25.8.2 --release trixie
./build.sh --armbian v26.08  --release noble

# 查看支持的版本
./build.sh --help
```

额外的 Armbian 参数放在 `--` 之后：

```bash
./build.sh --armbian v24.11.1 --release bookworm -- KERNEL_CONFIGURE=yes
```

## 编译产物

```
armbian/<版本>/output/images/
├── armbian_<ver>_Linuxbox-hubv3_<release>_current_6.6.158.img            # 原始磁盘镜像
└── armbian_<ver>_Linuxbox-hubv3_<release>_current_6.6.158.burn.img.zip   # Amlogic USB Burning Tool 镜像
armbian/<版本>/output/debs/linuxbox-bl706_<ver>_all.deb                   # BL706 烧录工具包
```

## 刷机

解压 `*.burn.img.zip`，在板子进入 USB 下载模式后，用 Amlogic USB Burning Tool 写入 eMMC。

首次启动时，通过串口（`ttyAML0`，115200 8N1）以 `root` / `1234` 登录，Armbian 会要求设置新密码。

## 设备上使用

```bash
# WiFi
nmcli dev wifi connect "<SSID>" password "<密码>"

# 蓝牙
hciconfig -a
bluetoothctl --timeout 15 scan on

# BL706（Zigbee 在 /dev/ttyAML3，Thread 在 /dev/ttyAML6）
/usr/lib/firmware/bl706/bl706_func.sh flash zigbee
/usr/lib/firmware/bl706/bl706_func.sh flash thread
```

## 目录结构

```
build.sh                    入口：克隆 Armbian、同步 userpatches/、调用 compile.sh
versions.conf               Armbian 版本及其支持的发行版
userpatches/
├── config/boards/linuxbox-hubv3.conf           板子定义
├── config/sources/families/linuxbox.conf       family：内核/u-boot 版本、FIP、扩展
├── config/kernel/linux-linuxbox-current.config 内核配置
├── kernel/linuxbox-6.6/                        内核补丁（Armbian meson64-6.6 + linuxbox-*）
├── u-boot/linuxbox-v2023.10/board_linuxbox-hubv3/  u-boot 补丁
├── bootscripts/boot-linuxbox.cmd               启动脚本
└── extensions/
    ├── linuxbox-hubv3.sh     /boot 只读、cgroup v1、BOARD_NAME
    ├── linuxbox-w1/          W155S1 WiFi/蓝牙固件、脚本、服务、aml_hciattach 源码
    ├── linuxbox-bl706/       BL706 烧录工具包（bflb_iot、固件、离线 Python 包）
    └── linuxbox-burn.sh      Amlogic 烧录镜像转换
armbian/<版本>/             上游 Armbian 源码（git 忽略，从不修改）
```

## 设计原则

- **Armbian 零修改。** 如果 Armbian 源码有本地改动，`build.sh` 拒绝编译。
- **只放硬件必需的内容。** 不包含应用层软件（Home Assistant、MQTT 等）。
- **内核与 U-Boot 固定**，所有 Armbian 版本在板子上的表现一致。

## 许可证

Armbian 采用 GPL-2.0。内核与 U-Boot 补丁遵循各自项目的许可证（GPL-2.0）。
固件按厂商提供的原样分发。
