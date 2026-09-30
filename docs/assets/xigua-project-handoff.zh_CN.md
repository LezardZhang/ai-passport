<p align="right">
  <a href="xigua-project-handoff.md">English</a> · <strong>简体中文</strong>
</p>

# 西瓜育儿助手项目交底

本文对应 `feature/xigua-childcare` 当前版本，记录产品方向、已经实现的模块、最近一次真机证据和后续工作。文档不包含 API 密钥或 Wi‑Fi 密码。

## 产品方向

远期目标是在 ESP32-C3 FoloToy AI Passport 上实现一个不依赖手机的育儿助手：断网时继续保存本地育儿记录，开机自动连接已知 Wi‑Fi，使用内置麦克风录下语音请求，交给 MiMo ASR 和语言模型处理，在屏幕显示可读回复，后续再通过扬声器播放 TTS 回复。

近期先完成真机语音验收，重新设计三键菜单的信息架构，增大并统一字体，完善长回复显示，增加可控的 ASR/TTS 深度自检，最后接入 TTS 播放。

## 已实现模块

- `main/xigua_app.c` 提供喂奶、尿布、睡眠、洗澡、趴玩和计时等育儿记录，使用 NVS 保存并支持撤销最近一次操作。Wi‑Fi 页面显示连接状态，提供“搜索附近 Wi‑Fi”按钮；从扫描结果选择 SSID 后，只用三页软键盘输入密码：大写、小写、数字/符号。
- `main/xigua_wifi.c` 保存上一次成功的 STA 配置，开机扫描后优先尝试三个内置网络，再尝试上次连接过的网络；所有者授权的内置配置保存在 `main/xigua_wifi_credentials.h` 并由 Git 管理。都不可见时进入本机“搜索并选择 Wi‑Fi”流程，不再启动蓝牙配网。认证过期、认证失败、关联失败和握手超时最多自动重试三次。连接前清除过期 BSSID 锁定，PMF 设为可选，连接阶段关闭省电，成功凭证会持久化。
- `main/xigua_ai.c` 使用配置好的 OpenAI 兼容 MiMo 接口，接入文本、ASR 和模型配置列表。`main/xigua_ai_credentials.h` 保存所有者授权的共享 endpoint、key 和模型配置，换机器克隆后不再需要重复本地设置；日志和交底文档不重复输出 key。录音为 16 kHz、16 bit、单声道 WAV，先写入 `voice_tmp` 分区，最长 60 秒，再分块 Base64 上传。录音和网络请求在后台任务执行，按键回调不阻塞。
- AI 后台任务在拿到 IP 后自动自检：先访问公共 HTTPS，再等待时间同步并发送最小 MiMo 文本请求。固件不再启动蓝牙配网服务，Wi‑Fi 与 TLS 共用的内存更充足。成功结果缓存六小时，失败后每两分钟重试；MiMo 请求同时发送标准 Bearer 鉴权和兼容旧网关的 `api-key`，失败日志保留有限响应片段。
- `main/xigua_font_zh16.c` 和 `main/xigua_font_zh20.c` 覆盖当前 UI 文案、标点和 ASCII 字符；主要中文、录音和自检提示使用较大字号。生成字体旁保留 LXGW WenKai 许可证。
- `partitions.csv` 为临时录音保留 `voice_tmp` 分区，同时保持 8 MB Flash 布局内的应用空间。

## Wi‑Fi 和 TLS 根因

反复连不上 Wi‑Fi 不是单纯的密码错误。串口显示 AP `Lezard2.4G` 已进入 WPA 认证，但返回 `WIFI_REASON_AUTH_EXPIRE (2)`；自动重试后使用 WPA2-PSK 关联成功并拿到 `192.168.50.115`。这符合 WPA2/WPA3 混合模式下认证握手偶发超时的特征。代码已经加入重试和 STA 配置规范化；如果必须消除首次失败，还应在路由器上固定 WPA2-PSK 做对照测试。

之前出现的 `ESP_ERR_HTTP_CONNECT` 是内存问题；移除蓝牙配网服务后，mbedTLS 不再与 NimBLE 争用堆空间。固件仍启用证书校验，并在 MiMo 返回非 2xx 时记录状态码和有限响应片段，便于区分网络、鉴权和请求格式错误。

普通刷写会覆盖 `0x0` 起的 bootloader、分区表和 factory 应用，但 `0x9000` 的 NVS 会保留。怀疑凭证异常时先使用 Wi‑Fi 清除操作；不要把整片 Flash 擦除当作常规修复，因为那会删除用户记录。

## 最近一次验证

- 构建：PASS。ESP-IDF 5.5.3、ESP32-C3、8 MB Flash。已验证归档：`build/firmware/e7e4f48e1c03c0f62063028e7e1c9dd7a3979e6838658a5a3aa020b1ebc6e455/`。
- Host/static 测试：PASS，包含仓库检查和 BSP 主机测试。
- 真机刷写与启动：PASS。合并镜像通过 USB Serial/JTAG 写入并通过 hash 校验。
- 真机 Wi‑Fi：在本次重试场景下 PASS。设备先记录一次 reason 2，随后自动重试，连接 `Lezard2.4G`，取得 IP 并启动 SNTP。
- 真机 HTTPS 与 MiMo 文本自检：PASS。安全版日志显示证书校验成功，公共探测返回 HTTP 200，MiMo 返回 200，文本自检为 `ESP_OK`。
- 最近一次刷入确认：PASS。设备这次直接以 WPA3-SAE 连接 `Lezard2.4G`，取得 `192.168.50.115`，证书校验成功，MiMo 自检完成，没有出现 HTTP 或 TLS 错误。
- 真机语音 ASR 与 TTS：本轮尚未完成端到端验收。录音和上传代码已经存在，但还需要在设备上确认一段实际麦克风语音、ASR 转写、模型回复和扬声器播放。

## 工作区检查点（2026-09-30）

最新 Wi‑Fi 界面、内置网络和共享 MiMo 配置已通过编译和合并镜像校验。刷写前会创建本地 Git 检查点。此前的搜索／选择实现已刷入实机并保留 NVS；最新镜像标识见下方。

- Wi‑Fi 代码已改向纯设备端流程：保留开机扫描和已保存网络重试；Wi‑Fi 页面现在搜索附近网络、选择 SSID，再用软键盘输入密码。西瓜助手路径已移除手动输入 SSID 和蓝牙配网。
- `main/xigua_wifi_security.c` 及其头文件已从西瓜助手构建中移除，应用配置中的 NimBLE/BLUFI 和组件依赖也已移除。基线 BLE 示例文件没有修改。
- MiMo 请求新增标准 `Authorization: Bearer` 鉴权，同时保留兼容旧网关的 `api-key`；请求使用 `max_tokens` 和 `enable_thinking=false`；非 2xx 回复会记录有限长度的错误响应片段。这只是代码修改，尚未在设备上确认能够修复 `ESP_FAIL`。
- 交底文档已更新为“搜索、选择 SSID、输入密码”的流程，并记录蓝牙配网已移除。
- Wi‑Fi 正文使用 16 px 字体并加高提示区；软键盘焦点使用 `< >`，模式和操作分为两行。界面文案避开照片中发现的缺失中文字形。
- 所有者已长期授权刷写：写入前保存本地 Git 源码检查点和匹配的固件、ELF、MAP，再直接刷写并保留 NVS，无需逐次确认。

本检查点验证结果：

- 已校验合并镜像 SHA-256：`DB213323011A34E496F35DA5BD7AA8672421C5C345F0412FAAF9DA3D6E1CBCB6`。匹配的固件、ELF、MAP 已归档到 `build/firmware/db213323011a34e496f35da5bd7aa8672421c5c345f0412faaf9da3d6e1cbcb6/`；ASCII 构建目录为 `C:/aihw_build_src/build/validation/`。
- 仓库检查：PASS（`python tools/check_repo.py`）。
- 深度睡眠契约测试：PASS（`python tests/test_deep_sleep_contract.py`）。
- `tests/test_check_repo.py`：FAIL，`VendoredDocumentationTest` 中有 5 个 vendored 文档断言失败；本检查点未修改这些断言。
- 固件构建：PASS。ESP-IDF 5.5.3 已生成 ESP32-C3 应用、bootloader、分区表和应用二进制。由于仓库路径含中文字符，当前主机的 `ldgen` 无法正确解析该路径，因此将同一工作树复制到只含 ASCII 字符的临时路径完成构建。
- 合并固件：PASS。`idf.py merge-bin` 已完成，`tools/verify_firmware.py` 已校验三个镜像、分区表、8 MB Flash 边界和 factory 应用位置。
- 完整静态门禁：NOT RUN，当前 PowerShell 主机没有可用 Bash 和 Host C 编译器，无法运行 `tools/validate.sh --static`。
- 本次真机刷写与启动：PASS。源码检查点为 `35034b86b169215ecad68e4fdf5a8cd05838f180`；COM6 的 ESP32-C3 已刷入上方归档中的分段镜像，写入哈希校验通过，NVS 保留。40 秒启动观察确认应用正常启动，扫描到 10 个网络并优先尝试内置 `GUANTANG_2.4G`。
- 本次真机 Wi‑Fi：FAIL。关联阶段返回 `reason=4`，随后进入本机搜索流程，未取得 IP；因此 MiMo 请求未运行。源码版本、镜像、ELF 和 MAP 已保留供恢复和排查。
- 本次最新固件的真机界面／密码输入、MiMo 请求和语音请求：NOT RUN，仍需按键观察以及成功联网。

下一步应执行剩余真机验收：完成搜索/选择/密码输入，修改凭据后重连，运行 MiMo 自检，并完成一次实际语音请求。`git status` 中原有的额度练习未跟踪文件需要继续保留。

## 尚未完成和待改进

字体覆盖已经改善，但在 240×320 屏幕上仍然偏小。菜单层级、焦点高亮、返回路径和底部提示行需要重新设计，不能继续靠增加提示文字解决。长模型回复还需要滚动或分页，并实现 UTF‑8 安全截断。

语音链路需要先测试短句，再测试 20–30 秒和 60 秒录音，同时记录录音时长、空闲堆、ASR 状态码、转写长度和模型回复。TTS 播放及音频格式适配尚未实现。自动自检暂时不调用 ASR/TTS，以避免每次开机消耗额度，后续应增加用户主动触发的深度自检。

为 Wi‑Fi 和语音状态机补充 Host 测试，并为字库覆盖建立自动检查。针对同一个 AP 做多次冷启动，统计首次认证失败概率。

## 接手步骤

修改前先阅读 `AGENTS.md`、五个必需 passport skill、Wi‑Fi 配网指南和本文。本私有仓库可以使用所有者授权的 Git 管理 Wi‑Fi 和 MiMo 配置；日志和交底文档不得输出这些值。交付前运行 `./tools/validate.sh --static`、`./tools/validate.sh --firmware` 和完整 gate。真机测试时从启动开始抓取串口，覆盖扫描、拿 IP、自检和语音请求；确认构建归档后，再从 `0x0` 刷入已验证的 `full.bin`。
