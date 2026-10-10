# CYD Pocket OS

**把一块 ESP32 小屏，变成随手可用的掌上应用桌面。**

[English](README.md) · [编译与烧录](docs/GETTING_STARTED.md) · [应用接入](esp32_system/APP_CONTRACT.md)

面向 ESP32-2432S028R 的轻量 Arduino 固件：240×320 竖屏、像素字体、分页主屏与底部上滑返回。不是 Android 移植，也不要求 PSRAM。

![主屏布局预览](assets/home_ui/page_1.png)

以上为主机渲染预览，不是实物屏幕照片。Deadlock 在第二页。

## 可以做什么

- **Spotify 遥控器**：封面、信息、进度条，MP3 风格触摸按键，切歌、音量、暂停，以及歌单／歌曲选择。遥控手机或电脑，不在 ESP32 上播放 Spotify 音频。
- **Dota 战绩**：OpenDota 数据，英雄头像与最近比赛结果。
- **Deadlock 战绩**：最近十场、每页四场；胜负、K/D/A、时长、UTC+8 开始时间与个人赛后魂魄总价值。社区服务可能遗漏比赛，魂魄不是未花余额。
- **Buddy**：斧王风格像素动画与 BLE/NUS 消息接入。
- **Synth**：触屏合成器，GPIO26 DAC 喇叭输出；初次使用请调低音量。
- **设置**：亮度与中英文偏好保存到 NVS；部分应用文字仍是英文。

## 面向小内存的实现

壳层统一管理返回手势和分页，网络请求放后台并设置超时／取消，头像存 Flash，Spotify 与 Deadlock 共用 TLS 客户端，Buddy 使用 NimBLE。提供串口诊断、堆检查、看门狗和回归脚本。

稳定性版本在一块板上通过 120 次串口驱动的应用切换；新版 Deadlock UI 又通过 24 次切换及三页往返。有限回归不保证永不卡死，也不是触屏自动化测试。真实 Spotify 切歌延迟、桌面 BLE 配对和音质仍需更多实机验证。

## 开始使用

按[安装指南](docs/GETTING_STARTED.md)安装依赖；将两个 `.example.h` 复制成对应的本地配置，填自己的 Wi-Fi、Steam Account ID 与 Spotify 凭据，再编译烧录。首次启动需完成四点触摸校准。

支持 ILI9341 + XPT2046 电阻触摸的 ESP32-2432S028R，其他型号可能需修改引脚／驱动。3 MB 应用分区不支持 OTA 双镜像。

**隐私提醒**：个人配置、日志和编译产物默认排除，固件本身包含凭据，不要上传个人构建。当前 HTTPS 使用 `setInsecure()`，不验证服务器证书；仅建议作为可信网络原型。详见 [SECURITY.md](SECURITY.md)。

欢迎提交实机照片、可复现问题和小型 PR。如果有帮助，欢迎点颗 star，让更多 CYD 玩家发现它。

原创代码使用 MIT；第三方代码保留许可。游戏角色／头像／商标不适用 MIT，详见[来源说明](THIRD_PARTY_NOTICES.md)。非 Valve、Spotify 或 OpenAI 官方项目。
