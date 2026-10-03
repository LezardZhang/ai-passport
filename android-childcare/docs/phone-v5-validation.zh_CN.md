<p align="right"><strong>简体中文</strong> · <a href="phone-v5-validation.md">English</a></p>

# 手机 0.5.0 验证 - 2026-10-03

三星 SM-S9280，Android16/API36，序列号 R5CX12FFTFF。同签名 `adb install --no-incremental -r` 将版本4覆盖升级到版本5 / 0.5.0-local，未清空数据、卸载或写入合成家庭资料。Android 接受覆盖安装，未执行数据库逐字节比较。拉取的实际安装 APK SHA256 `ae592a33d6ed741461442ad7d245725492ccb4623ccc25b0ae6da25c9ad2ffed` 与最终验证包一致。证据：安装（本地产物：`../reports/phone-v5-install.log`）、版本（本地产物：`../reports/phone-v5-version.txt`）、启动（本地产物：`../reports/phone-v5-launch.txt`）、页面/哈希回执（本地产物：`../reports/phone-v5-ui.json`）。

12项只读断言通过：照护语音入口、竖向时间线、直接记喂奶/换尿布、照护者入口、当前版本、自动检查、手动检查按钮、保留数据说明、旧空频道迁移后默认开启自动检查、返回照护页及实际安装包哈希。真机私人截图/XML保留在 `.local/phone-v5/`，不进入源码包。

首次接入阶段，手机访问内置 HTTPS 频道后显示未发布版本的固定提示；独立的无凭据健康请求通过正常证书验证，返回 HTTP404。该结果说明更新地址尚未部署，并不代表 OTA 成功。证据：公网健康检查（本地产物：`../reports/update-public-health-v5.json`）、实际部署状态（本地产物：`../reports/cloud-deployment-status-v5.json`）。

在本次首次接入阶段：实体手机 OTA 未运行，生产部署曾被浏览器自动审批阻断，等待明确的 1Panel 站点访问授权。实际下载/系统安装/数据保留已在隔离模拟器通过本机 HTTPS 服务和合成数据验证。本轮未测试真机语音准确率、最大磁盘/内存或生产容量。Build：PASS；Host tests：PASS；Device tests：PASS，范围限于上述真机只读检查及本机模拟器升级；Unverified：生产发布、真机 OTA 及运行容量。见[云端更新](cloud-updates.zh_CN.md)。

本文保留此前首次接入阶段的验收结果；后续已授权的线上部署和私人云端交付见 [cloud acceptance](cloud-acceptance.zh_CN.md).
