# 独立构建验证（2026-11-03）

- 主机：Ubuntu 26.04.1 LTS，x86_64，Python 3.14.4。
- 源码布局：将本目录单独解包至 SDK 树外的临时目录；新建输出目录，未复用旧应用对象文件。
- 构建入口：`bash build-ubuntu.sh SDK_ROOT ARM_DRM_PREFIX OUTPUT`。
- 工具链：SDK 自带 GCC 10.3 ARM musl 工具链。
- 外部输入：完整 Hi3516CV610 SDK 与既有 ARM libdrm release。
- 结果：camera_ai_lcd、wifi_setup 均成功构建、链接；file 确认为 ELF 32-bit ARM EABI5，动态加载器 /lib/ld-musl-arm.so.1。
- 打包：基础 camera-ai-lcd.tar.gz 生成成功，117 个文件逐项 SHA256 校验全部通过。
- 路径检查：生成的 flags.make、build.make、link.txt 未引用相邻 capture_picture/comm 或 SDK samples/mpp/common。
- 主机测试：菜单及 AI 开关、NV21 / 全屏裁剪 / 多目标叠层、Wi-Fi 运行包完整性与错误版本拒绝测试通过。

本次目录迁移没有改变运行逻辑。迁移前相同应用逻辑已完成实板部署及触摸开关：

```text
MODE_PROFILE ai=off frames=300 elapsed=25.832 fps=11.61
AI_TOGGLE enabled=1
MODE_PROFILE ai=on frames=30 elapsed=13.054 fps=2.30
```

独立目录本次构建验证不等于重新烧写系统，也未重复执行完整系统镜像构建。这轮早期测试使用 RAM 临时启动的 5.10.221 内核，当时尚未更新 NAND；之后的固化和自启测试见下文。可选服务和 WS73 运行包沿用此前独立验证结果，未包含于这次 117 文件基础包校验。

## 内核固化与开机自启（2026-11-03）

- 先备份原 kernel、env 和驱动，再将已经试运行的 Linux 5.10.221 写入 NAND kernel 分区；整个 4 MiB 分区回读比对一致。boot 分区未改写。
- 保存 mem=56M 启动参数，MPP、LCD、触摸和 WS73 使用匹配版本；MPP 使用其余 72 MiB。
- 注册 S99camera-ai-lcd，等待 sdk_server 就绪后进入摄像头预览，已保存 Wi-Fi 在后台重连。
- 最终服务脚本部署后执行普通软件 reboot：新启动标识确认已经重启，未手动启动应用，摄像头持续输出 FRAME，AI 开启时约 2.32 FPS。
- Wi-Fi 自动取得地址，只读 FTP 80 和 HTTP 相册 8080 均自动监听。重复 start 保持一个应用进程；stop、restart 均通过。
- BusyBox awk 输入 4005 行后日志保留最后 5 行，确认每 2000 行重新记录，防止逐帧日志持续占用 RAM。
- Ubuntu 独立构建和最终重打包成功，包含自启脚本、Wi-Fi 运行包和截图服务；139 个文件 SHA256 校验全部通过。
- 安装器默认注册自启；旧包缺少脚本时，在访问串口和网络前直接报错，可显式使用 --no-autostart。

以上重启测试为软件 reboot，未重复做物理断电测试。临时维护 SSH 授权在测试结束后移除。
