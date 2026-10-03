<p align="right"><strong>简体中文</strong> · <a href="cloud-updates.md">English</a></p>

# 私人云端更新，Android 0.6.6

Code12 / 0.6.5-cloud 是已发布的过渡版本，主清单为 `https://162.14.108.234/android-updates/latest.json`；`/cloud-backup/android-childcare-updates/latest.json` 保留为兼容回退。两个路由均指向独立更新容器，其 `XIGUA_UPDATE_PUBLIC_BASE` 已切为 `https://162.14.108.234/android-updates`。已有默认旧地址会迁移到新主地址；明确关闭或自定义频道保持原设置。沿用原安卓包名、签名和 SQLite 版本3。

设置→版本与更新显示当前版本、检查状态、上次成功检查、版本说明、下载和安装确认。默认打开应用时检查，每6小时最多一次，可关闭自动检查；发现新版后照护首页会提醒。检查不会自动下载或安装。安装由 Android 确认，可能要求允许此应用安装更新包。

下载使用 HTTPS，不跟随重定向；专用下载凭据只发送到更新清单的同源站点。暂存前校验 SHA256、实际包名/版本/最低系统及原安装签名；安装前再次检查暂存哈希和签名。频道改变后旧请求不能覆盖新状态；失败或取消会清理临时文件，升级完成后清理已安装的候选包。记录、人物、设置及待同步操作留在应用数据库中。

## 当前版本

Code13 / 0.6.6-cloud 将照护者登记简化为一个称谓输入框，已从新主入口发布。两个清单别名返回同一版本，APK 地址使用 `/android-updates/apks/`；两个鉴权 APK 别名均匹配本地签名包。本次功能改动需要新版本号，未重复发布 code12。见[Code13 验证](../reports/caregiver-title-delivery-v13.json)。

## 主人的长期授权

2026年10月3日，后端协调任务转达主人对现有私人育儿 APK 和 `162.14.108.234` 更新服务的明确“长期授权”，来源任务为 `01a0fd0e-7c7c-7761-b5a2-27fc30907d1f`。沿用此前已授权范围，后续常规构建、签名校验、发布、本机及服务器发布凭据配置、范围限定的 Token 读取与内置私人 APK，以及相关私人项目 Git 同步，无须重复询问主人。见[授权记录](../reports/owner-authorization-20261003.json)。

范围仍为育儿更新目录 `/home/clouddata/android-childcare-updates/`、相关 HTTPS 代理配置和路由，以及此前已授权从 `cockpit-cloud-cloud-backup-1` 限定读取 `PERSONAL_CHILDCARE_TOKEN`、通过现有接口读取宝宝档案和照护资料。发布凭据仍保存在发布器及服务器配置中，不进入 APK；仅已授权的应用、下载及业务 Token 可内置，凭据值不得出现在日志、报告或交付摘要中。授权不扩大到管理员密钥、无关私人文件、成人资料、其他应用或业务数据库修改。Git 同步仅覆盖主人私人项目中的相关改动，不授权公开分发凭据或同步无关工作。工具运行时的审批要求，以及当前协调任务对 Chrome/1Panel 的操作限制，仍然有效。

本记录不改变产物或验收结果：code13 保持已发布，无须为了授权或同步核查新增版本。ADB 未识别手机时，code13 真机安装仍未验证；真实家庭同步仍需要缺失的专用家庭凭据。

## 发布操作

保留原 `.local/signing/childcare-local.jks` 和私人 `.local/update-publisher.json`；后者包含 `public_base/publish_token/download_token/certificate_sha256`，其中 `public_base` 已切为 `https://162.14.108.234/android-updates`，凭据及预期签名保持原值。主人授权的应用配置仅内置范围限定的下载凭据，发布凭据不进入 APK、日志或源码包。APK 包含主人授权的 AI 配置，因此下载安装包需要鉴权。

增加安卓清单中的 `versionCode/versionName` 并验证功能改动后，在 `android-childcare/` 运行：

```sh
./tools/release-update.sh --notes-file /path/to/release-notes.txt
```

该命令执行主机检查、沿用原签名构建、通过 SDK `apksigner/aapt` 验证实际 APK，上传并回读带鉴权的最新清单。普通构建不会发布。`python3 tools/publish-update.py prepare` 仅离线验证；`check` 读取云端版本。发布工具拒绝错误签名/包名、测试 APK，以及版本号未增加却改变内容的发布。编码元数据须小于7.6KiB，版本说明过长会明确拒绝；始终使用正常 HTTPS 证书校验。

独立服务验证上传大小/哈希及有界 APK 压缩目录，实际加密签名由发布工具和手机验证；服务本身不运行安卓 SDK 签名检查。它只接受指定包名和签名声明，因此发布权限使用独立凭据。

## 隔离部署

部署包仅含 `backend/app/android_updates.py`、已固定版本的运行依赖、Dockerfile/Compose 和拟加入的 Nginx 路由片段，不重建统一后端或迁移业务数据。已部署服务器目录 `/home/clouddata/android-childcare-updates/`；新服务仅监听 `127.0.0.1:8916`，现有 HTTPS 代理将专用更新前缀转发给它。预检已确认端口空闲及实际 TLS 虚拟主机，备份所改代理文件，检查 Nginx 配置后再加载，保留旧配置用于回滚。

发布按流上传一个 APK，验证后保存版本号/哈希组成的不可变文件，文件持久化后才原子切换 `latest.json`。上传失败保留旧版，重复相同发布可安全重试，历史 APK 留存；不自动删旧包，不提供应用数据库降级。坏版本通过更高版本号的修正版回退功能。

服务配置上限：一个工作进程/上传、120秒上传期限、APK64MiB、编码元数据头8KiB、ZIP4096项/中央目录2MiB、磁盘储备128MiB、发布总量2GiB及1000个不可变文件。晋升中断可复用已校验的文件，主人历史查询最多返回100条。容器配置限制192MiB内存及64个进程；配置上限不是生产峰值测量。见[资源预算](resource-budget.zh_CN.md)。

## 历史首次接入验证

本地验证：安卓主机129项断言、后端85项用例、安卓回归354项断言、更新专属45项安卓断言。真实签名的版本5 APK 已通过本机 HTTPS 服务完成发布与回读，使用专用测试 CA 和合成凭据。合成版本6 / 6-test-only 只用于本机安装验收，正式发布工具拒绝发布。证据：主机（本地产物：`../reports/host-tests-v5.log`）、后端（本地产物：`../reports/backend-suite-v5.log`）、安卓回归（本地产物：`../reports/android-tests-v5.log`）、更新检查（本地产物：`../reports/android-cloud-update-v5.log`）、HTTPS发布（本地产物：`../reports/update-local-publish-v5.log`）、仓库完整验证（本地产物：`../reports/repository-gate-v5.log`）。

隔离模拟器通过真实 HTTPS 下载合成版本6，在正常应用更新页打开 Android 系统确认框，点击 Update 后完成覆盖安装。验证照护记录及人物归属、宝宝设置、服务配置哈希和待同步操作ID均保留，已安装候选包清理通过。测试CA只用于测试APK传输，生产应用沿用标准TLS。最终版本5 APK 的每个非签名条目均与已验收运行代码相同。证据：安装保真（本地产物：`../reports/update-install-continuity-v5.log`）、正常应用安装界面（本地产物：`../reports/update-installer-ui-v5.log`）、运行代码一致性（本地产物：`../reports/update-runtime-continuity-v5.json`）、限定范围私人部署包回执（本地产物：`../reports/update-deployment-bundle-v5.json`）。

主人已授权限定范围部署及 1Panel 站点访问，独立服务已经**上线**。现有 TLS 虚拟主机通过 `/opt/1panel/www/sites/docforge/proxy/android-childcare-updates.conf` 加入专用路由；原虚拟主机和云备份路由的哈希保持不变，备份位于 `/home/clouddata/android-childcare-updates/proxy-backup-20261003T071444Z`。Nginx 检查/加载及公网标准 HTTPS 证书验证通过；无凭据清单/APK请求，以及下载凭据访问主人历史，均返回401。真实版本5和正式版本6已发布并回读，鉴权下载 APK 字节/哈希与本地签名包一致。见[部署回执](../reports/update-cloud-deployment.json)、[公网健康检查](../reports/update-public-health-cloud.json)、[发布验证](../reports/update-cloud-verification-v6.json)。

在版本5/6首次接入阶段，三星手机已识别线上版本，发现0.5.1-cloud，完成下载并通过正式应用的 APK 校验，当时系统安装等待首次安装来源授权。这个历史结果不代表当前 code12 的安装状态。生产容器一次内存抽样为32.77MiB，配置上限192MiB；抽样不代表峰值或负载容量。见[云端验收](cloud-acceptance.zh_CN.md)、运行代码一致性（本地产物：`../reports/update-runtime-continuity-v6.json`）。


## 过渡地址

更新客户端与 Cloud Backup 独立。新主路由已经生效，服务器和私人发布器均使用 `/android-updates/`。旧 `/cloud-backup/android-childcare-updates/` 继续供已安装旧客户端使用，也作为过渡客户端的回退地址。两个别名均已提供 code12，鉴权清单完全一致。不可变的 code12 元数据保留原旧 APK 地址；两个别名的鉴权 APK 下载字节均与本地签名包一致。后续版本使用新主路径的 APK，无须重复发布 code12。受支持的旧客户端收到同签名过渡 APK 后，才能移除旧别名。见[当前迁移验证](../reports/update-migration-v12.json)。

更新入口同步仅修改本地发布配置和文档，未重复发布 code12 或重建生产服务。之后独立的照护者功能改动产生了 code13，未修改 Cloud Backup 数据。
