<p align="right"><strong>简体中文</strong> · <a href="cloud-acceptance.md">English</a></p>

# 私人云端交付验收 — 2026-10-03

主人授权新增 `/home/clouddata/android-childcare-updates/` 目录、独立更新服务、相关 HTTPS 代理配置备份及专用更新路由。部署使用已授权的现有 1Panel 登录会话，直接进入限定目录，未读取或修改现有业务数据库。自动审批曾拒绝浏览 `/home` 总览，之后改为直接进入授权目录，未使用其他入口绕过拒绝。

私人部署包 SHA256 为 `64b5ec864055c55b85b2e73c02e3caff900b200ddc91ca4ffe89312ddacc684c`；启动 Python3.12 容器前逐项校验了8个载荷文件。服务仅监听 `127.0.0.1:8916`，配置192MiB内存及64进程上限，实际容器健康状态为 healthy。发布后一次抽样为32.77MiB/192MiB、2个进程，未测量峰值内存或负载容量。

实际 IP TLS 虚拟主机为 `/opt/1panel/www/conf.d/docforge.conf`，由 `1Panel-openresty-ahKZ` 提供服务。其原有代理目录中新加入 `android-childcare-updates.conf`，原虚拟主机和云备份路由哈希保持不变，两者备份位于 `/home/clouddata/android-childcare-updates/proxy-backup-20261003T071444Z`。新增前后 Nginx 配置检查通过，并完成平滑加载。见[限定部署回执](../reports/update-cloud-deployment.json)、部署截图（本地产物：`../reports/update-cloud-deployment.jpg`）。

## 当前照护者版本 code13

Code13 / 0.6.6-cloud 已沿用原签名发布。照护者登记现在只有“我是孩子的 ___”，新记录使用这个称谓，已保存的历史归属保持原值。两个清单别名均返回 code13，APK 地址使用 `/android-updates/apks/`，通过两个别名鉴权下载的字节均匹配本地签名 APK。安卓编译、134项主机断言通过；隔离安卓照护者持久化验证通过16项断言，正常登记页已目视确认只有一个输入框。手机目前未出现在 ADB 中，code13 真机安装仍待完成。家庭同步凭据仍缺失，因此不声称真实家庭资料已完成云端同步。见[Code13 交付](../reports/caregiver-title-delivery-v13.json)、登记页面（本地产物：`../reports/caregiver-title-form-v13.png`）。

## 路由迁移与 code12 验收

10月3日，新主路由 `/android-updates/` 已生效。负责部署的任务报告仅重建独立更新容器，将 `XIGUA_UPDATE_PUBLIC_BASE` 切为 `https://162.14.108.234/android-updates`，未重建 Cloud Backup。本地只读验证确认两个健康入口返回200，无凭据清单返回401，鉴权清单完全一致，两个鉴权 APK 别名的字节均匹配本地签名 code12 产物。

本地私人发布器已切到新主 URL，凭据及预期签名保持原值。Code12 元数据保留原旧 APK 地址，新服务器主地址用于后续版本，无须重复发布 code12 或仅为迁移提升版本。三星 SM-S9280 已沿用原签名安装 code12 / 0.6.5-cloud。本次验证在执行任何手动查询之前，正常更新页显示自动检查开启、“已是最新版本 0.6.5-cloud”，上次成功检查时间为17:55。普通页面不显示成功频道来源，因此仅凭页面状态无法区分主入口和回退请求。见[迁移验证](../reports/update-migration-v12.json)。

## 历史版本5/6发布与手机验证

| 检查 | 结果 |
| --- | --- |
| 公网 HTTPS 健康及标准证书校验 | PASS，HTTP200 |
| 无凭据清单和实际 APK 请求 | PASS，401拒绝 |
| 下载凭据访问主人发布历史 | PASS，401拒绝 |
| 发布者访问历史 | PASS，HTTP200 |
| 真实签名版本5发布/回读 | PASS |
| 三星正常应用检查线上版本5 | PASS，识别0.5.0-local |
| 正式版本6 / 0.5.1-cloud 发布/回读 | PASS |
| 鉴权下载 APK 字节/哈希 | PASS，与本地签名包一致 |
| 三星正常应用发现/下载/暂存校验 | PASS |
| 三星 Android 系统安装 | 等待首次安装来源授权 |

正式版本6 APK SHA256 为 `a8b5a54babc425badf5b97fc204ab9e9b85664d0e6af1133226c418701af1fa2`，DEX、资源和内置配置与已验收版本5运行代码一致，仅版本清单和签名材料不同，沿用原签名证书。合成版本6 / `6-test-only` 夹具未发布到云端，也未安装到实体手机。见运行一致性（本地产物：`../reports/update-runtime-continuity-v6.json`）、[云端验证](../reports/update-cloud-verification-v6.json)、手机云端下载（本地产物：`../reports/phone-cloud-download-v6.json`）。

三星 SM-S9280 / Android16 API36 已进入“西瓜育儿”的首次“安装未知应用”权限页面，已请求主人仅授权该应用作为安装来源；等待答复期间不更改权限，也不声称系统安装完成。已保存升级前时间线、家庭和服务配置界面的指纹，不导出私人内容；安装后再比较。未清空真机数据、卸载、写入合成家庭资料或运行测试插桩，私人真机截图/XML保留在 `.local/phone-cloud/`。

## 后续更新

将安卓版本号增加到高于当前已发布版本，保留原签名并验证功能改动，在 `android-childcare/` 执行 `./tools/release-update.sh --notes-file /path/to/notes.txt` 即可构建、校验、发布并回读私人云端版本。发布 APK 无须重建服务器或修改代理。打开应用最多每6小时检查一次；设置→版本与更新提供手动检查，下载与 Android 安装确认仍需明确操作。见[云端操作](cloud-updates.zh_CN.md)。

历史版本6验证 — Build：PASS，实际版本6 APK 及官方 SDK 签名/包名检查；Host tests：PASS，安卓129项断言，相同运行代码已有后端85项用例、安卓回归354项断言及更新专属45项断言验收，部署后当前仓库完整验证也通过，见完整验证（本地产物：`../reports/repository-gate-v6.log`）；Device tests：PASS，范围为真实公网 HTTPS/发布及真机发现、下载和暂存校验；Unverified：真机系统安装及资料保留比较、服务器峰值/负载容量、最低手机更新内存/磁盘储备。
