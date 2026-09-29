[English](xigua-m2.md) · **简体中文**

# 西瓜助手 M2 联网与 AI 文本增量

本阶段在 [M0–M1 原型](xigua-m0-m1.zh_CN.md)之上加入 USB 运行时配置、8 个 Wi-Fi 配置、SNTP 校时、MiMo 文本请求入口、当地日期统计和一致性 USB 记录导出。全量 M2 和语音版尚未完成。用户当前远程操作，现场验收暂缓；本轮不烧录、不打开设备串口、不调用真实付费 API。

## 已实现的行为

- 网络 worker 独占 Wi-Fi STA。优先连接较小 priority 数值的配置；同优先级轮换，失败指数退避，稳定连接不因其他 AP 更强而漫游。密码错误、找不到 AP 和其他原因保留状态码。
- USB 配置成功表示已持久化，尚不证明 Wi-Fi 密码正确或服务可用。回复含 saved 和 reboot_required；后者为 true 时需重启应用配置。
- 每次启动都需 SNTP 首次校时才信任绝对时间。临时断网保留本次启动时钟；同次启动的未知记录可重建时间并持久化。跨启动的不确定区间保留原有语义。
- 时区由 USB 手动设置；空字符串表示待配置。支持固定 POSIX TZ（例如 CST-8、UTC0），以及带明确 M 规则的 DST（例如 EST5EDT,M3.2.0/2,M11.1.0/2）。不接受 IANA 名称，也不声称自动定位。时钟和时区可用后，今天页按当地日界线统计；上下键切换累计。
- 设置页显示网络、时间、AI 是否配置及最近请求结果。AI 问答通过 USB 电脑工具显示，屏幕不承诺任意中文回复的字形覆盖。
- AI 调用在独立 worker 中运行，不持有 LVGL 锁；本地按键和记录仍由原控制任务处理。请求前检查内存，内存不足返回 low_memory，设备实际峰值仍待测。

## USB 工具

电脑需要 Python 和 pyserial。烧录并确认设备端口后，使用 [xigua_device.py](../../tools/xigua_device.py)。以下 COM6 只是示例，操作前确认实际端口。

```text
python tools/xigua_device.py --port COM6 status
python tools/xigua_device.py --port COM6 export --output xigua-records.json
python tools/xigua_device.py --port COM6 wifi-set --slot 0 --ssid Home --priority 0
python tools/xigua_device.py --port COM6 time-set --timezone CST-8
python tools/xigua_device.py --port COM6 ai-set --endpoint https://api.xiaomimimo.com/v1/chat/completions --model mimo-v2.6-flash
python tools/xigua_device.py --port COM6 test-ai
python tools/xigua_device.py --port COM6 ask "Reply hello briefly."
python tools/xigua_device.py --port COM6 wifi-clear --slot 0
python tools/xigua_device.py --port COM6 ai-clear
```

密码和 API Key 使用隐藏提示输入，不放入命令行参数。SSID 最多 32 UTF-8 字节；密码为空（开放网络）、8–63 个可打印 ASCII 字符，或 64 位十六进制 PSK。slot 为 0–7，priority 为 0–255；可用 --disabled 暂存停用配置。AI Key 最多 191 字节，model 最多 63 字节；endpoint 是完整 HTTPS 请求地址，拒绝账号信息、查询参数、片段及控制字符。当前使用 MiMo 的 api-key 鉴权头，不能假定兼容要求 Bearer 的其他提供商。

只有 test-ai/ask 会主动调用服务，可能计费；配置操作不会自动测试 API。工具不自动重试，超时表示结果未确认，应先查状态。串口打开前关闭 DTR/RTS，不主动复位。USB 管理接口面向可信本机访问，不是远程管理接口；收到 id/op/未知字段、重复字段、非法 UTF-8 或畸形 JSON 会拒绝。请勿把管理串口转发给不可信网络。

## AI 协议和界限

依据 [MiMo 官方文档](https://mimo.mi.com/docs/zh-CN/quick-start/summary/model)，默认候选模型为 mimo-v2.6-flash，endpoint 为 https://api.xiaomimimo.com/v1/chat/completions。使用 api-key、stream=false、thinking.type=disabled、max_completion_tokens=256。

输入上限 512 UTF-8 字节，JSON 响应 8 KiB，返回文本最多 1,024 字节并按 UTF-8 边界截断，truncated 字段明确标注。DNS、TLS 和 HTTP 共用 30 秒期限，按块读取并限制头部和总响应；验证服务器证书和主机名，不跟随重定向，不自动重试。401/429 等保留 HTTP 状态码；失败不输出服务端原始错误正文或凭据。

这是无历史上下文的单次文本请求。未开放模型工具；工具调用响应会拒绝，模型说“已记好”也不会写入记录。没有加入 ASR、TTS、PTT、Jev 或设备上问答页面。真实账号权限、额度、DNS/TLS 可达性与模型返回尚未验证。

## 配置与数据

沿用 M0–M1 的分区位置和 128 条事件上限。运行时配置保存在 xigua_data 分区的 settings 命名空间，育儿记录仍在 xigua 命名空间。配置采用版本化 JSON、双 NVS 槽和 CRC；成功提交后才发布配置，损坏或不确定提交转只读，不自动擦除。修改网络不清历史，ai-clear 不删除网络。

密钥不写入源码或固件常量。当前 NVS 未启用加密；清除操作不是物理安全擦除，旧槽或 NVS 页仍可能保留旧值。不要分享设备的原始 Flash 转储。

Schema 1 在 128 条事件和 16 条重放记录下的最大快照为 8,975 字节，主机测试对此断言。快照缓冲由各 20,000 字节收紧为 9,216 字节。关闭 Wi-Fi IRAM 加速，静态 RX 缓冲为 4、动态 RX/TX 上限各 8、RX BA 窗口为 4，以峰值吞吐换取 RAM；运行时余量仍需实测。

## 验证和后续

运行 tools/validate.sh；主机测试包括配置校验/往返、严格 JSON、Wi-Fi 策略、AI 响应解析、模拟串口关联/超时/短写。协议测试使用 tests/vendor/cjson 下未修改的 cJSON.c/.h 和 MIT LICENSE，来源是 ESP-IDF 5.5.3 所固定的 [cJSON 提交](https://github.com/DaveGamble/cJSON/tree/c859b25da02955fef659d658b8f324b5cde87be3)。该副本仅用于测试；固件链接 IDF 自带 json 组件，静态测试无需安装 IDF。

构建和具体镜像哈希见本轮交付报告；编译通过不代表现场验收。仍需实测双 AP 切换、密码错误、断网/校时、凭据掉电恢复、USB 反复连接、真实 API 和网络忙时按键/内存峰值。BLUFI、自动时区、本地声音、语音录放与安全工具执行继续保留在后续计划中。

## 当地日期统计

今天页显示奶瓶次数/奶量、尿布、洗澡、睡眠和趴玩时长。默认查看当前当地日期；上下键切换今日与累计，OK 查看所有已保存记录。时钟或时区不可用时明确显示暂不可统计，不把未知值显示为精确的零。无法归日的记录单列待确认，不计入今日合计；累计仍保留已知次数和时长。

日界线按配置的 POSIX TZ 计算相邻两个日历午夜，而不是直接加 86,400 秒；夏令时日期可为 23 或 25 小时。进行中与已结束的睡眠/趴玩按当天区间分摊。同次启动的时长保持单调时间口径，从记录起点锚定分配，后续时钟跳变不会凭空增加时长。端点未知或无法还原的区间仍标待确认。时区变更只重算日界线，不改历史 UTC；原始时间经过重建时，统计仍带有该估算性质。

## 记录导出

export 命令只读导出当前保留的全部事件，不修改设备。内容包含事件 ID/版本、类型/数值、进行中状态、起止时钟及可信度、已保存时长及可信度。文件格式为 xigua-records-v1；64 位时间戳和时长使用十进制字符串，避免整数精度丢失。不包含 Wi-Fi/AI 凭据、设置、计时器、撤销/重放内部状态。这是可读的记录存档；尚未实现恢复导入命令。

协议分为 records.begin、records.item、records.finish。逐条读取和结束校验都携带初始启动 ID、状态版本和记录数，控制任务每次只复制一条有界记录。任何记录修改或时间重建都会使版本失效并返回 snapshot_changed。工具不自动重试；停止记录操作后可重新执行。最后一次校验可发现读取最后一条后的变更。电脑只在整段数据校验完成后发布 JSON 文件，拒绝覆盖已有目标，超时或失败不会留下半份目标文件。无法读取或原始数据损坏时，不会把空状态冒充成功导出；已验证并恢复的只读快照仍可导出。

进行中的事件导出已保存的起点和时长字段，不虚构结束时间或不断刷新的已过时长。导出文件含个人记录，请妥善保管。真实 USB 导出和掉电恢复仍需上板验收。

本地声音已在后续 [M3 增量](xigua-m3.zh_CN.md)中实现，实机验收仍待完成。
