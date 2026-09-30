[English](README.md) · **简体中文**

# CSI Scope 0.1 固件

目标：FoloToy AI Passport / ESP32-C3 / 8 MB Flash / ESP-IDF 5.5.3。

使用 GitHub 的原始文件/下载按钮下载 [CSI-Scope-full.bin](CSI-Scope-full.bin)。这是完整合并镜像，烧录到 `0x0`。它会替换原应用，并可能重置 Wi-Fi 等设置。无需全片擦除。

```bash
python -m esptool --chip esp32c3 --port <PORT> --baud 460800 write_flash 0x0 CSI-Scope-full.bin
```

命令使用原 ESP-IDF 5.5.3 环境中的 esptool 4.x 语法。将 `<PORT>` 替换为设备串口。

SHA-256：`052047148b311e6b7852b7019d9711ff66e5b4d467cdc661aaf987f01bc99bf1`。
所有分发文件的校验值见 [SHA256SUMS.txt](SHA256SUMS.txt)。同时附带应用、引导程序、分区表镜像及 `flash_args`，供分段烧录使用。

## 验证范围

- Build：原完整门禁 PASS；发布前再次验证固件布局、合并镜像及对应 ELF。
- Host tests：原门禁 PASS；107 项 Python 测试中，21 项因 Windows 符号链接权限限制跳过。
- Device tests：烧录、设备端哈希校验、启动、配网和首次 CSI 接收 PASS。短时观察有效速率为 29–61 帧/秒，不保证固定速率。
- Unverified：校准完成及静止/走动分数对比、全部按键和中文字形、错误密码恢复、重启重连及长期稳定性。

附带二进制为先前已验证的构建，内嵌版本是 `0b9e4c8-dirty`。此次发布补充文档并整理已有产物，未重新编译固件。源码修改基于上游提交 `0b9e4c81ee4421c0bac39ca3561d65a8285acd4a`。不承诺产品级灵敏度或检测准确率。

完整源码位于本仓库。配网、按键及限制见 [项目 README](../../README.zh_CN.md)。