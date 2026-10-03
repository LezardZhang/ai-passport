<p align="right"><strong>简体中文</strong> · <a href="sync-storage-audit.md">English</a></p>

# Code13 同步与存储核查

2026年10月3日的本地核查未发现西瓜育儿 0.6.6-cloud 自动上传整包备份的链路，无须修改应用、重新构建或发布。APK SHA256 与已发布 code13 回执一致，本次核查涉及的9个源码文件均匹配 code13 构建清单。见[机器可读证据](../reports/sync-storage-audit-v13.json)。

| 链路 | 实际行为与依据 |
| --- | --- |
| 自动照护同步 | 启动和记录变动会安排联网任务；另有按 Android 允许的时机执行的周期任务，请求周期为15分钟。`SyncService` 调用 `Api.sync()`，不会导出压缩包。见[调度](../app/src/main/java/cn/xigua/childcare/SyncService.java)、[启动](../app/src/main/java/cn/xigua/childcare/CareApp.java)。 |
| 记录上传 | 仅将待同步 outbox 操作提交到 `/sync/push`，每个 JSON 请求包含一个操作，每次运行最多处理100个。操作包含受影响的一条记录，并非仅字段差异，也不是整个数据库快照。见[接口](../app/src/main/java/cn/xigua/childcare/Api.java)、[队列选择](../app/src/main/java/cn/xigua/childcare/Store.java)。 |
| 回执和重试 | 成功回执会移除已确认的 outbox 行。失败或回执丢失时可能重试原冻结操作 ID 和载荷，这是单条记录重试，不是反复上传 ZIP 备份。 |
| 下载及其他请求 | 拉取使用修订游标、宝宝档案和最多100条变更的分页。能力、档案及可选宝宝资料请求均为 JSON 读取；可选后端 AI 任务是独立 JSON 操作，不是备份。 |
| ZIP 导出 | 生产代码中 `BackupArchive.write` 的唯一调用者，是用户主动导出后，文档选择器请求51返回成功的处理逻辑。写入用户选择的文档 URI；若用户选择云端文档服务，该次主动导出可能远程保存，但 APP 本身不会调度或上传 ZIP。`allowBackup=false` 关闭了 Android 系统备份。见[导出界面](../app/src/main/java/cn/xigua/childcare/MainActivity.java)、[压缩包](../app/src/main/java/cn/xigua/childcare/BackupArchive.java)、[清单](../app/src/main/AndroidManifest.xml)。 |
| APK 发布 | 本地发布器仅在显式执行时，将签名 APK 提交到独立 `/android-updates/publish`。普通构建与安卓 APP 均不会发布版本。见[发布器](../tools/publish-update.py)、[发布命令](../tools/release-update.sh)。 |
| 更新检查 | 自动检查最多每6小时获取一次清单，APK 下载需要点击下载按钮。主更新路由和旧路由均代理到8916端口的独立服务；旧 Cloud Backup URL 前缀只是兼容别名，并不使用通用备份存储。见[更新客户端](../app/src/main/java/cn/xigua/childcare/Updates.java)、代理（协调项目源码：`../../backend/deploy/android-updates.nginx.conf`）、独立版本目录（协调项目源码：`../../backend/deploy/android-updates.compose.yml`）。 |

内置家庭数据接口仍为 `/cloud-backup/app/v1`，与版本存储分离。Code13 配置没有家庭凭据，因此全新安装且本机没有已存凭据时，会在发送数据请求前停止。本次核查不声称真实家庭同步成功、当前真机配置、线上实际请求流量或服务器存储保留策略已验证；未操作服务器、Chrome 或 1Panel。

Build：本次核查 NOT RUN，已有 code13 构建 PASS；Host tests：本次核查 NOT RUN，已有 code13 主机验证134项断言 PASS；Device tests：本次核查 NOT RUN，ADB 没有连接的实体设备，code13 安装仍待完成；Unverified：code13 真机验收、家庭凭据配置与实际家庭同步。
