<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# 西瓜育儿 Android

原生私人家庭应用，包名`cn.xigua.childcare`，最低Android8/API26、目标35。**0.6.6-cloud/版本号13**沿用原本地签名及SQLite版本3，覆盖升级保留记录、照护者和待同步操作。手机端固定为一个私人家庭和一个宝宝：底部只有照护、记录、家庭，AI、同步、备份和更新统一放在设置。

## 安装

APK（本地产物：`build/apk/xigua-childcare-0.6.6-cloud.apk`）：`adb -s SERIAL install -r build/apk/xigua-childcare-0.6.6-cloud.apk`。
保留`.local/signing/childcare-local.jks`用于后续兼容升级；源码包不包含该签名私钥。新机器生成不同签名无法覆盖当前安装。

## 日常使用

| 入口 | 用途 |
| --- | --- |
| 照护 | 一个语音/键盘入口分流记录、问答、故事、播放、提醒和历史。所有候选记录均先进入确认表单再保存。 |
| 记录 | 按日期时间线、本地统计、补记、编辑/删除/撤销及翻页。 |
| 家庭 | 宝宝资料、照护者称谓登记/修改/切换/停用/恢复。 |

不需要蓝牙或Wi-Fi配网，网络由Android管理。照护工具收纳音频/提醒/交接；使用系统文档选择、通知、媒体焦点及安装流程。

## 语音与自由AI配置

点击说话后，**录音中点发送**会结束录音、等待识别并只提交一次。结束录音返回可编辑文字；转写中发送等待当前任务，重复/迟到/取消结果不会写入记录。允许麦克风后自动继续；退后台取消当前录音和待发送。识别失败保留一份私人录音，可重试识别或取消语音。定时刷新不会重建录音/转写中的输入区。

AudioRecord以16kHz单声道16位PCM写文件，**没有固定60秒录音截止**，保留磁盘保护。长录音按有界WAV段顺序识别，不把整段音频或Base64复制进堆。应用自行录音并调用所选云识别，无需Google识别组件。

设置→**AI服务与模型**可保存最多20套具名Base URL/API Key。对话、ASR、TTS、视觉理解和JEV判断独立选择服务与模型；获取模型调用带鉴权的`GET /models`，返回ID可选择，接口不可用时可手填。鉴权支持Bearer和API-Key；语音接口支持MiMo音频消息或兼容的multipart `/audio/transcriptions`。返回模型列表不代表都支持语音；连接检查和实际调用会显示失败。修改默认配置只影响后续任务，正在处理的请求保留发起时配置。

私人APK按主人授权内置匹配**https://token-plan-cn.xiaomimimo.com/v1**的密钥。默认对话模型`mimo-v2.6-flash`，语音`mimo-v2.5-asr`；报告不显示密钥值。第二版已填写的设置迁为独立档案。云备份仍需自己的服务凭据；APK 更新服务与 Cloud Backup 分离：新版本默认使用 `/android-updates/`，同时保留 `/cloud-backup/android-childcare-updates/` 作为旧版本过渡回退。

喂养量支持中文数字和小数，留空表示未知。否定/复合指令要求确认；补记时间及睡眠结束时间须明确填写。照护者登记只有“我是孩子的 ___”一个称谓输入框，例如爸爸、爷爷、奶奶，无须姓名或电话。新记录用称谓署名，云端沿用同一个称谓；已有资料优先使用原关系，缺少关系时保留旧称呼。每条记录可选择实际照护者并保留当时称谓；归属快照随记录同步到兼容后端，改名或停用不改写历史，不提供云账号、邀请或权限认证；目前支持一个宝宝身份。

## 数据、媒体和维护

SQLite记录/发件箱事务保留UUID重试、修订、墓碑及清空代次保护。可选Personal API备份将服务/档案身份冻结在操作中，兼容对象型档案发现；身份缺失或变化会隔离旧队列而不重新绑定。旧记录编辑和睡眠结束继承原身份；明确处理冲突才生成新操作。本地夹具测试不代表生产备份验收。手机端现在导出真正的ZIP备份包，包含清单、NDJSON快照、可校验结构和明确的媒体清单；事务校验完成前不提供恢复入口。CSV仅保留为内部兼容格式。

单一前台MediaPlayer/MediaSession负责焦点、耳机拔出及停止。本地导入每项128MiB、总256MiB，离线雨声可用；重启不自动播放，可明确恢复进度。朗读依赖已安装中文语音。提醒和交接保持本机，调度依赖Android。

版本维护默认检查独立私人 HTTPS `/android-updates/`，必要时回退到旧 `/cloud-backup/android-childcare-updates/` 清单和下载路径，显示版本说明，下载并验证更高版本的同包名同签名 APK，再由 Android 确认安装。保留本机数据并清理已安装的候选包；旧空更新地址独立于服务档案完成迁移。最多每6小时检查一次，下载与安装须明确操作。见[云端更新与部署](docs/cloud-updates.zh_CN.md)；新主路由已上线，私人发布器也已切换至新入口。两个路由现均提供 code13 / 0.6.6-cloud，并使用新主路径的 APK 地址；旧别名继续保留，供旧客户端获取具备过渡能力的 APK。

三星 SM-S9280 已通过 ADB 升级为 code12 / 0.6.5-cloud，沿用原签名并保留应用数据。两个线上更新别名返回相同的鉴权清单及 APK 哈希。早期版本5/6云端下载检查保留为历史证据，见[云端验收](docs/cloud-acceptance.zh_CN.md)、[首次接入验收](docs/phone-v5-validation.zh_CN.md)。

主人对范围限定的 APK 维护、Token 配置及私人项目 Git 同步的长期授权，已记入[云端操作](docs/cloud-updates.zh_CN.md#主人的长期授权)，沿用原范围及当前验收限制。[同步与存储核查](docs/sync-storage-audit.zh_CN.md)确认记录操作与主动 ZIP 导出使用独立流程。

## 构建与验证

JDK17、Android SDK35/build-tools35.0.1/platform-tools、Python3；`JAVA_HOME/ANDROID_SDK_ROOT`指定路径。框架原生视图/SQLite，直接使用aapt2/javac/d8/zipalign/apksigner，不依赖WebView运行时。源码检出包含空凭据模板；构建更新前，按[配置准备](config/README.zh_CN.md)恢复已授权的私人配置及原签名。本次 Git 同步不改变已发布 code13 APK。

```sh
./tools/test-host.sh
./tools/build.sh
```

在后端Python环境启动`tools/local-api-fixture.py`，再运行`./tools/device-test.sh emulator-NNNN`检查一次性真实后端集成。设备/UI夹具脚本拒绝真机序列号；`native-ui-smoke.py`只重置隔离模拟器并修改其字号/旋转。合成升级APK仅用于测试。真实提供方测试使用生成语音，不写私人记录。

[当前手机框架](design/phone-v7-framework.zh_CN.md)、[历史设计](docs/phone-v3-design.zh_CN.md)、[实施计划](docs/phone-v3-plan.zh_CN.md)、[UI规范](design/android-phone-design.zh_CN.md)、[原型](design/prototype.html)、[架构](docs/architecture.zh_CN.md)、[资源预算](docs/resource-budget.zh_CN.md)、[历史交付证据](docs/delivery.zh_CN.md)。原型回应为模拟。按主人授权将安卓源码、空凭据模板、配对文档及选定脱敏回执同步到 Git，根仓库和后端无关工作不进入本提交；未刷固件。已在授权范围发布独立私人更新服务和签名 APK。[时间线设计与验证](docs/caregiver-timeline.zh_CN.md)、[实施计划](docs/caregiver-timeline-plan.zh_CN.md)。
