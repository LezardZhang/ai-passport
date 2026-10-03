<p align="right"><strong>简体中文</strong> · <a href="delivery.md">English</a></p>

# Android 0.4.0 本地交付

安卓和管理端宝宝页新增竖向每日时间线，显示实际照护者、发生时间、动作，以及奶量或尿布细节；两端都有直接喂奶和换尿布入口。安卓保留宝宝、照护者登记、编辑与停用，每条记录保存当时的姓名快照；补记可另选照护者。未知时间和细节保持未知，并单独分组。见[产品与兼容协议](caregiver-timeline.zh_CN.md)。

照护者快照现可通过共用个人 API 同步。旧客户端省略字段时保留现有快照，明确 null 时清除；v3 已冻结操作原样重放，随后补发归属更新。不支持的服务让具名记录留在待同步队列，不丢弃姓名。冲突处理中保留本地，也保留本地照护者选择；只修改照护者时，保留自定义喂养内容、可空尿布类型、原发生时间、区间和独立提供的睡眠时长。原身份隔离、成人编辑器和持久化网页同步队列继续保留。

## 成品与验证

APK（本地产物：`../build/apk/xigua-childcare-0.4.0-local.apk`）：**0.4.0-local / code 4**，270,877 字节，SHA256 `e81687b9b1533a8f1ca01d1773b21217d2ecb7fe1ae7d289dff919168114ea46`。包名 `cn.xigua.childcare`，最低 API 26 / 目标 35。证书 SHA256 `b9ba60dca0104e1968bdd6fa46e9459799bb2e4b69f2dd8d41c14a70321917a9`，沿用原本地签名，APK v2/v3 签名验证通过；SQLite 仍为版本 3。基线 `8978fa86bba2b40554fda9e051b0f8f7d7ea5801`，修改尚未提交。

| 字段 | 结果 |
| --- | --- |
| Build | PASS 签名/对齐 APK、ESP-IDF 5.5.3 完整仓库门禁；未改固件、未刷机 |
| Host tests | PASS 125 项安卓断言、77 项后端测试（含 Node 显示/表单回归），以及仓库静态/主机检查 |
| Device tests | PASS 隔离 API 35 模拟器上 354 项完整安卓断言、36 项照护者协议断言、20 项原生界面断言 |
| 原生界面 | PASS 时间排序、小数奶量、明确选择照护者、旋转恢复、自定义喂养内容保留；已检查日间及夜间 200% 字体截图 |
| 真机 | PASS 三星 SM-S9280 / Android 16 API 36 同签名升级、已安装 APK 哈希、启动及 13 项只读页面/表单检查 |
| Unverified | 本次扩展的生产部署；真机麦克风/人声识别准确率；TTS/音质、来电/耳机/长后台/重启提醒/TalkBack/API 26；负载容量和共享账户/名单/邀请/发布服务 |

证据：构建清单（本地产物：`../reports/build-manifest-v4.json`）、完整仓库门禁（本地产物：`../reports/repository-gate-v4.log`）、主机（本地产物：`../reports/timeline-host-tests.log`）、后端（本地产物：`../reports/timeline-backend-suite.log`）、安卓（本地产物：`../reports/android-tests-v4.log`）、照护者同步（本地产物：`../reports/timeline-android-tests.log`）、日间原生界面（本地产物：`../reports/timeline-native-ui-tests.log`）、夜间原生界面（本地产物：`../reports/timeline-native-ui-night-tests.log`）、实际网页表单（本地产物：`../reports/timeline-web-ui.json`）、[真机](phone-v4-validation.zh_CN.md)、交付清单（本地产物：`../reports/delivery-manifest.json`）。功能测试使用一次性数据，不使用生产账号。完整回归之后仅调整截图测试脚本；最终 APK 的每个非签名条目均与实机已验收版本一致。

一次独立审查发现四项实质保真问题：冲突恢复丢失照护者、原生编辑替换未知/自定义细节、擅自填写历史时间、网页重算未修改的睡眠事实。失败回归先复现问题，再验证修复；也修复了未知睡眠时长提示和网页未知时间分组。没有剩余 Critical 问题。界面测试改为等待可见控件、新注入的输入组件和旋转恢复完成，避免读到隐藏或过期按钮。

## 运行与归档

语音继续使用原生 AudioRecord 和配置的 ASR 服务。发送会结束录音、等待文字、提交一次，再打开记录确认；没有固定 60 秒截止，真实连续录音 65 秒已通过。失败录音保留私人重试/丢弃草稿。多套具名 URL/key、接口模型发现、手填模型、对话/ASR 独立绑定，以及主人授权的内置 Token Plan 配置继续保留；常规证据和摘要不显示密钥。

本机照护者名单可含联系电话；同步的仅是每条记录的姓名/ID 快照。本次未实现远程账户、共享名单管理或邀请。管理端扩展已本地测试，**尚未部署生产**；服务声明支持照护者之前，具名记录会留在手机等待同步。大规模历史或平台分配不能视为已验证容量，见[资源预算](resource-budget.zh_CN.md)。

版本维护接受更高版本、同包名/原签名 APK，可选下载另验清单；未配置线上发布频道。code 5 升级夹具仅测试，不包含在交付中。保留签名密钥供后续升级，旧 APK 不代表数据库可降级。

源码/证据包（本地产物：`../build/source/xigua-childcare-0.4.0-local-source.zip`）包含 app/tools/tests/docs/design、选取的 v4 证据、APK 和当前后端源码/协议快照，附逐文件哈希。主人授权的提供方配置和 APK 有意包含；私有签名、本地运行时/模拟器、用户数据库、真机截图/XML及合成升级夹具排除。归档验证（本地产物：`../reports/source-archive-v4.json`）。后端只修改时间线协议和界面集成，保留其他并行工作；未提交、推送或发布生产。

历史 v3 归档（本地产物：`../build/source/xigua-childcare-0.3.0-local-source.zip`）和 [v3 实机验证](phone-v3-validation.zh_CN.md)保持不变。
