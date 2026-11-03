# CV610 Camera AI LCD

这是一个在 CV610 开发板上运行的摄像头应用：用 OS04D10 采集画面，在 JD9853 屏幕上显示实时预览和识别结果。右上角的触屏菜单可以配 Wi-Fi、开关 AI、查看帧率，也可以截图。

项目单独编译，不需要放进官方 samples 目录。摄像头初始化代码已放在 `vendor/hisilicon/comm/`，海思 SDK、模型、交叉工具链和 ARM libdrm 需要另外准备。

## 目录

- [功能](#功能)
- [运行截图](#运行截图)
- [硬件与运行环境](#硬件与运行环境)
- [Ubuntu 构建](#ubuntu-构建)
- [Wi-Fi 与截图服务打包](#wi-fi-与截图服务打包)
- [安装与启动](#安装与启动)
- [开机自启](#开机自启)
- [屏幕操作](#屏幕操作)
- [截图下载](#截图下载)
- [验证记录与测试](#验证记录与测试)
- [目录结构](#目录结构)
- [常见问题](#常见问题)
- [来源与许可](#来源与许可)

## 功能

```text
OS04D10 → VI / ISP → VPSS NV21 → AIComponent → 画面 + 多目标框 / 类别 / 置信度
                                                          ↓
                                                JD9853 LCD + 触摸菜单
```

- 摄像头采集 2560×1440 / 15 FPS，送入 AI 的画面为 768×432，屏幕画布为 320×240。
- `--models hvf`：人脸、人体、车辆；`--models all` 加入宠物、包裹。使用 SDK 专用模型，不支持任意物体或人名识别。
- 一帧内可以标出多个目标。AI 默认开启，关闭后仍可看摄像头预览。
- 可在屏幕上查看设备和网络信息，扫描 Wi-Fi 并输入密码连接。
- 截图保存为 PNG，检测结果另存为 JSON；电脑可通过网页相册或只读 FTP 下载。

## 运行截图

下面几张是板端画面截图，分辨率为 320×240。

| 全屏主界面 | 悬浮菜单展开 | 画面统计开启 |
| --- | --- | --- |
| ![摄像头全屏预览与右上角悬浮按钮](docs/images/main-screen.png) | ![向下展开的五项图标菜单](docs/images/main-menu.png) | ![分辨率、采集帧率与应用帧率叠层](docs/images/main-stats.png) |

统计页里，15 FPS 是摄像头采集帧率，约 2.3 FPS 是开启 AI 后的显示帧率。这几张图直接读取了 framebuffer，所以能看到菜单；用屏幕上的截图按钮保存时，菜单不会入镜。

绿色纸盒被识别为 PACKAGE，置信度 55%：

![包裹识别效果：绿色纸盒被框出并标记 PACKAGE 55%](docs/images/detection-package.png)

下面这张里，手部被标成了 HUMAN，置信度也是 55%。这是一次误检，留着方便对照模型的实际表现。

![指定识别效果图：画面中的手部被模型标为 HUMAN 55%](docs/images/detection-human-hand.png)

| 主板信息 | 摄像头与屏幕 | 无线模块 |
| --- | --- | --- |
| ![主板信息](docs/images/device-board.png) | ![摄像头信息](docs/images/device-camera.png) | ![无线模块信息](docs/images/device-radio.png) |

设备信息页可以查看摄像头型号、分辨率和帧率，也能查看有线网络、Wi-Fi 和无线模块状态。

## 硬件与运行环境

| 项目 | 要求 |
| --- | --- |
| 主板 | Sunniwell CV610 / Hi3516CV610 |
| 摄像头 | OS04D10，MIPI-CSI 2-Lane，接 Sensor0 |
| 拨码 | SW1301：1-OFF、2-OFF；断电接线和调整 |
| 屏幕 | JD9853 240×320，Hynitron 触摸 |
| 无线 | WS73；2.4 GHz 开放网络或 WPA/WPA2-PSK |
| 构建机 | Ubuntu x86_64；实测 26.04.1 LTS / Python 3.14.4 |
| 板端 | 匹配的 Linux、设备树、MPP、LCD、触摸及 WS73 模块；sdk_server 已运行 |

编译命令在 Ubuntu x86_64 上执行，生成的 ARM 程序放到开发板运行。

安装前先检查板端有没有 `/dev/dri/card0`、触摸 evdev 设备和 `/run/sdk_server/osal_uds_server.sock`。这些依赖内核和驱动，需要提前准备好。运行时先停掉其他占用摄像头、NPU 或屏幕的示例。

这套应用使用 Linux **5.10.221**，MPP、LCD、触摸和 WS73 模块都要与它匹配。版本末尾的 `+` 也有影响，**5.10.221+** 的旧模块不能混用。调试时可以从 RAM 启动内核；要让应用重启后继续运行，还需要把匹配的内核写入 NAND。蓝牙目前只做 HCI 状态显示。

## Ubuntu 构建

编译前需要先完成[官方首个项目教程](https://docs.hisilicon.com/projects/hi_aiot_solution/master/get-started/first-project/index.html)，准备好完整源码、SDK、工具链和模型。ARM libdrm 从已编译成功的 JD9853 panel 工程中取得。

```bash
sudo apt update
sudo apt install -y build-essential cmake python3 python3-serial git

git clone https://github.com/kirovl/hisi-sample.git
cd hisi-sample
APP="$PWD"
PROJECT="$HOME/workspace/hi_aiot_solution_cv610_ev_board/sdk_workspace/hi_aiot_solution"
SDK="$PROJECT/sdk/Hi3516CV610"
# 替换成已成功构建 JD9853 panel 示例的完整工程路径
PANEL_PROJECT="$HOME/workspace/cv610-panel/sdk_workspace/hi_aiot_solution"
DRM_PREFIX="$PANEL_PROJECT/_build/open_source/drm/release"
OUT="$HOME/workspace/camera-ai-lcd-output"

test -f "$DRM_PREFIX/lib/libdrm.a"
bash "$APP/build-ubuntu.sh" "$SDK" "$DRM_PREFIX" "$OUT"
file "$OUT/build/camera_ai_lcd" "$OUT/build/wifi_setup"
(cd "$OUT/package" && sha256sum -c SHA256SUMS)
```

编译后会得到两个程序 `build/camera_ai_lcd` 和 `build/wifi_setup`，以及 `package/` 目录和 `camera-ai-lcd.tar.gz` 安装包。`file` 的输出应包含 ARM EABI5。模型和运行库会从 SDK 一起复制过来，输出目录请放在 SDK 库目录之外。

如果还要使用 Wi-Fi 和截图下载，接着打包下面的运行文件。

## Wi-Fi 与截图服务打包

```bash
python3 "$APP/build-services-ubuntu.py" "$SDK" "$OUT/services"
# 替换为已准备好的、与板端内核匹配的官方 WS73 rootfs 暂存目录
WS73_ROOTFS="$HOME/workspace/ws73-rootfs"
python3 "$APP/wifi-provision/package-runtime.py" "$WS73_ROOTFS" "$OUT/wifi-runtime"

WIFI_RUNTIME="$OUT/wifi-runtime" SERVICE_BUILD="$OUT/services" \
  bash "$APP/build-ubuntu.sh" "$SDK" "$DRM_PREFIX" "$OUT"
```

把 `WS73_ROOTFS` 改成已经准备好的 WS73 rootfs 目录。仓库里不附带这部分文件，目录中需要有 `root/plat_soc.ko`、`root/wifi_soc.ko`、`drv/ko/ot_board_io_cfg.ko`、`root/wpa_supplicant`、`root/wpa_cli`、`lib/libsecurec.so` 与 `ws73/` 固件。脚本会检查文件是否齐全、模块版本是否匹配，并生成一份空的 Wi-Fi 配置。

也可通过 `wifi-provision/build-userspace.sh PROJECT OUTPUT` 从 SDK 构建 OpenSSL / libnl / supplicant。若遇到驱动版本不匹配，仍需处理 WS73 内核模块，重新编译这些用户态程序没有用。

需要重编内核时，可以使用 `build-wifi-display-kernel.sh PANEL_PROJECT SDK OUTPUT`。它沿用编译成功的 panel 工程和设备树，去掉版本号末尾额外的 `+`，启用 WIRELESS、CFG80211、MAC80211，并重新编译 MPP、LCD 和触摸模块。

这个脚本需要完整 SDK 和已经配置好的 panel 构建目录，只生成文件，不执行烧写。生成后先从 RAM 启动测试。

## 安装与启动

先让开发板进入 Linux，把 Debug USB 接到 Ubuntu，退出其他串口终端并停掉旧预览。确认 Ubuntu 能访问开发板，再按实际情况修改下面的 IP 和串口路径。

```bash
SERIAL_PORT=/dev/serial/by-id/usb-1a86_USB_Single_Serial_5CF9102052-if00
BOARD_IP=192.168.3.70
ping -c 3 "$BOARD_IP"
sudo python3 "$APP/install-ubuntu.py" "$OUT/camera-ai-lcd.tar.gz" \
  --board-ip "$BOARD_IP" --port "$SERIAL_PORT"
```

安装脚本通过 TCP 18889 传文件，校验 SHA256 后解压到 `/opt/camera_ai_lcd`，并设置开机自启。加 `--no-autostart` 可以跳过自启设置。这个脚本只安装应用，内核和驱动仍需提前准备。

在板端 shell 执行：

```sh
sh /opt/camera_ai_lcd/camera-service.sh start
```

启动后会直接显示预览，Wi-Fi 在后台连接，取得 IP 后启动截图服务。应用由后台服务管理，停止时使用下一节的 `camera-service.sh stop`。调试启动脚本时，可以设置 `CAMERA_AI_SKIP_WIFI=1` 跳过 Wi-Fi 管理。

需要单独调试时，先用 `camera-service.sh stop` 停掉后台应用，再执行：

```sh
sh /opt/camera_ai_lcd/run.sh --models hvf --seconds 10
sh /opt/camera_ai_lcd/run.sh --headless --models all --frames 10
```

## 开机自启

新安装包默认创建 `/etc/init.d/S99camera-ai-lcd`。已有安装可以在开发板上执行：

```sh
sh /opt/camera_ai_lcd/enable-autostart.sh enable
sh /opt/camera_ai_lcd/camera-service.sh start
sh /opt/camera_ai_lcd/camera-service.sh status
```

开机时，服务会等 `sdk_server` 准备好再启动预览，不必等 Wi-Fi 连上。日志在 `/tmp/camera-ai-lcd.log`，每 2000 行重新记录，避免长时间运行后占满内存。

如果装在其他目录，把命令中的路径换掉即可，注册脚本会记录实际位置。已有服务运行时，不要另开预览进程。

```sh
# 停止、重启应用
sh /opt/camera_ai_lcd/camera-service.sh stop
sh /opt/camera_ai_lcd/camera-service.sh restart

# 关闭下次开机自启，不影响当前进程
sh /opt/camera_ai_lcd/enable-autostart.sh disable
```

如果启动时报 `Kernel mismatch`，先用 `uname -r` 检查板端内核，再核对打包的驱动版本。

2026-11-03 在板上执行普通重启后，预览、Wi-Fi、FTP 80 和网页 8080 都已自动恢复。过程记录在 [VALIDATION.md](VALIDATION.md)。

## 屏幕操作

点右上角的半透明按钮，会向下展开五个图标；再点菜单外的画面就能收起。按钮固定在右上角。

1. 圆圈 i：设备信息，NETWORK / BOARD / CAMERA / RADIO 页签，BACK 返回。
2. 柱状图：开关画面统计，显示摄像头分辨率、采集 FPS、VIEW FPS、AI ON/OFF、目标及截图数。
3. Wi-Fi：扫描、上下滑动列表、选择热点、输入密码连接；已连接热点标绿。
4. 相机：保存摄像头画面、框与标签；截图不包含导航菜单或设备信息页。
5. AI：开关识别，开启时图标高亮。关闭后识别框消失，摄像头继续预览，帧率统计也会重新计算。

在配网页点 BACK 可以返回预览，不会断开已有连接。目前支持开放网络和 WPA/WPA2-PSK，不支持企业认证、WEP 和仅 SAE 的 WPA3，中文热点名也可能显示不全。密码保存在板端配置文件中，信息页不显示密码。

## 截图下载

截图放在 `/opt/camera_ai_lcd/screenshots/`，文件名为 `shot-*.png`，同名 JSON 记录检测结果。图片是 320×240 横屏。开启画面统计时，统计也会一起保存，悬浮菜单不会入镜。除了触屏按钮，也可以发送 SIGUSR1 触发截图。

打包了截图服务后，应用会在后台启动它。需要手动启动时执行：

```sh
sh /opt/camera_ai_lcd/serve-screenshots.sh start
# 停止：sh /opt/camera_ai_lcd/serve-screenshots.sh stop
```

以板端 Wi-Fi `192.168.3.70` 为例：

- 浏览器相册：<http://192.168.3.70:8080/>。
- 只读 FTP：`ftp://192.168.3.70:80/`，匿名，用 FileZilla 或 curl。

浏览器打开 8080 端口，FTP 客户端连接 80 端口，两个地址不要用反了。FTP 只提供下载，不支持上传、删除、TLS 或 SFTP。服务日志在 `service-runtime/`，下载截图不会删除板上的文件。

```bash
curl --fail --user anonymous: --list-only ftp://192.168.3.70:80/
```

## 验证记录与测试

2026-11-03，把项目单独放到 SDK 目录外，从头编译并打包。预览和配网程序均编译成功，基础包中 117 个文件的 SHA256 校验通过。加入 Wi-Fi、截图服务和自启脚本后的完整包为 139 个文件，也已全部校验。构建时没有借用相邻 sample 的源码，详细记录见 [VALIDATION.md](VALIDATION.md)。

在屏幕菜单里切换 AI 开关，测得的显示帧率如下：

| 模式 | 实测应用 FPS |
| --- | ---: |
| AI 开启，三模型 | 2.30 |
| AI 关闭 | 11.61 |

两种模式下摄像头都以 15 FPS 采集。目前预览和推理串行执行，AI 会拖慢显示；关掉后显示帧率提高了约 5 倍，但模型仍留在内存中。摄像头实测识别过 FACE、HUMAN 和 PACKAGE，PET、VEHICLE 还没做现场测试。

通用测试可在 Mac / Ubuntu 执行：

```bash
cc -std=c99 -O2 -I include tests/test_menu.c src/menu.c src/overlay.c -lm -o /tmp/cv610-test-menu
/tmp/cv610-test-menu
cc -std=c99 -O2 -I include tests/test_overlay.c src/overlay.c -lm -o /tmp/cv610-test-overlay
/tmp/cv610-test-overlay
python3 -m unittest discover -s tests -p 'test_*.py'
```

`test_touch.c` 用到 Linux evdev，需要在 Ubuntu 上运行。`tests/board_smoke.py` 则需要连接开发板，并准备好 SDK 测试素材和板端运行环境。

## 目录结构

```text
src/                     相机、推理、DRM、触摸、菜单、Wi-Fi、截图
include/                 应用接口
vendor/hisilicon/comm/    随项目提供的初始化辅助源码
wifi-provision/          WS73、网络重连、用户态构建及运行包
build-ubuntu.sh          独立 CMake 构建和基础打包
build-services-ubuntu.py  独立 BusyBox FTP/HTTP 构建
install-ubuntu.py         网络安装与校验
start-application.sh     预览和后台 Wi-Fi 入口
camera-service.sh        应用启动、停止与状态
enable-autostart.sh      注册或移除开机启动入口
gallery/                 截图相册
tests/                  主机与实板验证
```

## 常见问题

| 现象 | 处理 |
| --- | --- |
| 构建时缺少 SDK 或 libdrm | 核对外部路径与 ARM 版本；先完成官方 SDK 和 panel 构建 |
| undefined reference: ot_ivs_md_* | 保留 CMake 中显式链接的 ss_ivs_md |
| Ubuntu 执行 ARM 程序报 Exec format error | 构建产物应在开发板运行 |
| invalid module format | 核对完整内核版本，末尾 `+` 也必须一致 |
| 无 /dev/dri/card0 | 检查显示内核、设备树和模块，安装应用不能代替驱动适配 |
| 摄像头 I2C 首寄存器写失败 | 断电检查 OS04D10 FPC、Sensor0、两个 OFF |
| scan failed | 检查 WS73 模块、CFG80211、supplicant 及版本 |
| 串口 Resource busy | 停止其他串口会话后再安装 |
| 相册打不开 | 核对实际 IP、HTTP 8080 和服务日志 |
| 重启后屏幕不亮 | 检查 NAND 内核版本、S99camera-ai-lcd 和 /tmp/camera-ai-lcd.log |

## 来源与许可

海思辅助源码来自 [HiSpark CV610 仓库](https://gitcode.com/HiSpark/hi_aiot_solution_cv610_ev_board) 的 `samples/capture_picture/comm`，保留 HiSilicon 版权与 Apache-2.0 声明，见 [vendor/hisilicon/README.md](vendor/hisilicon/README.md)。带 Apache-2.0 标注的源码遵循 [Apache License 2.0](LICENSE)。外部 SDK 库、模型、固件和驱动分别遵循其原许可，不包含于本仓库。

截图服务可选构建的 BusyBox 遵循其自身许可；构建器在运行包中保留许可证及配置，对应源码来自 SDK 的 busybox-1.34.1。分发服务二进制时应同时满足其源码提供要求。
