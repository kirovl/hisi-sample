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

独立目录本次构建验证不等于重新烧写系统，也未重复执行完整系统镜像构建。Wi-Fi、MPP、LCD 和触摸的实板环境仍为 RAM 临时启动的 5.10.221 内核；NAND 未更新。可选服务和 WS73 运行包沿用此前独立验证结果，未包含于这次 117 文件基础包校验。
