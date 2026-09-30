<p align="right">
  <a href="xigua-project-handoff.md">English</a> · <strong>简体中文</strong>
</p>

# 西瓜育儿助手项目交底

本文对应 `feature/xigua-childcare` 当前版本，记录产品方向、已经实现的模块、最近一次真机证据和后续工作。文档不包含 API 密钥或 Wi‑Fi 密码。

## 产品方向

远期目标是在 ESP32-C3 FoloToy AI Passport 上实现一个不依赖手机的育儿助手：断网时继续保存本地育儿记录，开机自动连接已知 Wi‑Fi，使用内置麦克风录下语音请求，交给 MiMo ASR 和语言模型处理，在屏幕显示可读回复，后续再通过扬声器播放 TTS 回复。

近期先完成真机语音验收，重新设计三键菜单的信息架构，增大并统一字体，完善长回复显示，增加可控的 ASR/TTS 深度自检，最后接入 TTS 播放。

## 已实现模块

- `main/xigua_app.c` 提供喂奶、尿布、睡眠、洗澡、趴玩和计时等育儿记录，使用 NVS 保存并支持撤销最近一次操作。Wi‑Fi 页面显示连接状态，支持三个内置网络、手动输入 SSID/密码和三页软键盘：大写、小写、数字/符号。
- `main/xigua_wifi.c` 保存上一次成功的 STA 配置，开机扫描后优先尝试上次连接过的网络，再尝试三个内置网络；都不可见时进入手动或 BLE 配网。认证过期、认证失败、关联失败和握手超时最多自动重试三次。连接前清除过期 BSSID 锁定，PMF 设为可选，连接阶段关闭省电，成功凭证会持久化。
- `main/xigua_ai.c` 使用配置好的 OpenAI 兼容 MiMo 接口，接入文本、ASR 和模型配置列表。录音为 16 kHz、16 bit、单声道 WAV，先写入 `voice_tmp` 分区，最长 60 秒，再分块 Base64 上传。录音和网络请求在后台任务执行，按键回调不阻塞。
- AI 后台任务在拿到 IP 后自动自检：先访问公共 HTTPS，再等待时间同步并发送最小 MiMo 文本请求。自检前停止 BLUFI/NimBLE，Wi‑Fi 页面仍可重新启用配网，从而为 TLS 释放内存。成功结果缓存六小时，失败后每两分钟重试。
- `main/xigua_font_zh16.c` 和 `main/xigua_font_zh20.c` 覆盖当前 UI 文案、标点和 ASCII 字符；主要中文、录音和自检提示使用较大字号。生成字体旁保留 LXGW WenKai 许可证。
- `partitions.csv` 为临时录音保留 `voice_tmp` 分区，同时保持 8 MB Flash 布局内的应用空间。

## Wi‑Fi 和 TLS 根因

反复连不上 Wi‑Fi 不是单纯的密码错误。串口显示 AP `Lezard2.4G` 已进入 WPA 认证，但返回 `WIFI_REASON_AUTH_EXPIRE (2)`；自动重试后使用 WPA2-PSK 关联成功并拿到 `192.168.50.115`。这符合 WPA2/WPA3 混合模式下认证握手偶发超时的特征。代码已经加入重试和 STA 配置规范化；如果必须消除首次失败，还应在路由器上固定 WPA2-PSK 做对照测试。

之后出现的 `ESP_ERR_HTTP_CONNECT` 是另一个内存问题。自检时 BLUFI/NimBLE 仍占用内存，mbedTLS 做 RSA 证书校验时最大连续堆只有 18 KB，导致 RSA 临时分配失败。停止配网后最大连续堆提升到 32 KB，最新安全版已经通过证书校验，公共 HTTPS 和 MiMo 请求均返回 `ESP_OK`。最终固件没有关闭证书验证。

普通刷写会覆盖 `0x0` 起的 bootloader、分区表和 factory 应用，但 `0x9000` 的 NVS 会保留。怀疑凭证异常时先使用 Wi‑Fi 清除操作；不要把整片 Flash 擦除当作常规修复，因为那会删除用户记录。

## 最近一次验证

- 构建：PASS。ESP-IDF 5.5.3、ESP32-C3、8 MB Flash。已验证归档：`build/firmware/e7e4f48e1c03c0f62063028e7e1c9dd7a3979e6838658a5a3aa020b1ebc6e455/`。
- Host/static 测试：PASS，包含仓库检查和 BSP 主机测试。
- 真机刷写与启动：PASS。合并镜像通过 USB Serial/JTAG 写入并通过 hash 校验。
- 真机 Wi‑Fi：在本次重试场景下 PASS。设备先记录一次 reason 2，随后自动重试，连接 `Lezard2.4G`，取得 IP 并启动 SNTP。
- 真机 HTTPS 与 MiMo 文本自检：PASS。安全版日志显示证书校验成功，公共探测返回 HTTP 200，MiMo 返回 200，文本自检为 `ESP_OK`。
- 最近一次刷入确认：PASS。设备这次直接以 WPA3-SAE 连接 `Lezard2.4G`，取得 `192.168.50.115`，证书校验成功，MiMo 自检完成，没有出现 HTTP 或 TLS 错误。
- 真机语音 ASR 与 TTS：本轮尚未完成端到端验收。录音和上传代码已经存在，但还需要在设备上确认一段实际麦克风语音、ASR 转写、模型回复和扬声器播放。

## 尚未完成和待改进

字体覆盖已经改善，但在 240×320 屏幕上仍然偏小。菜单层级、焦点高亮、返回路径和底部提示行需要重新设计，不能继续靠增加提示文字解决。长模型回复还需要滚动或分页，并实现 UTF‑8 安全截断。

语音链路需要先测试短句，再测试 20–30 秒和 60 秒录音，同时记录录音时长、空闲堆、ASR 状态码、转写长度和模型回复。TTS 播放及音频格式适配尚未实现。自动自检暂时不调用 ASR/TTS，以避免每次开机消耗额度，后续应增加用户主动触发的深度自检。

为 Wi‑Fi 和语音状态机补充 Host 测试，并为字库覆盖建立自动检查。针对同一个 AP 做多次冷启动，统计首次认证失败概率。

## 接手步骤

修改前先阅读 `AGENTS.md`、五个必需 passport skill、Wi‑Fi 配网指南和本文。凭证只放在被忽略的本地头文件中。交付前运行 `./tools/validate.sh --static`、`./tools/validate.sh --firmware` 和完整 gate。真机测试时从启动开始抓取串口，覆盖扫描、拿 IP、自检和语音请求；确认构建归档后，再从 `0x0` 刷入已验证的 `full.bin`。
