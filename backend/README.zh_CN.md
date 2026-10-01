<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# 西瓜育儿后端

该服务接收 ESP32-C3 育儿助手的事件记录，并为儿歌、故事和白噪音提供音频
目录。单台腾讯云服务器使用 SQLite，数据库和音频文件都放在 `/data`。

本地运行：

```bash
cd backend
python -m uvicorn app.main:app --reload
```

部署时复制 `.env.example` 为 `.env`，为设备、管理端、Hermes 和公开读取
分别设置随机 token，设置管理密码和 `CHILDCARE_PUBLIC_BASE_URL`，然后执行
`docker compose up -d --build`。容器监听 8000 端口，命名卷
`childcare-data` 持久化 SQLite 和音频。把音频文件放到 `/data/media`，通过
`POST /v1/audio/tracks` 登记，设备使用返回的 `play_url`。正式对外使用前，
应在 8000 端口前配置 HTTPS 反向代理，管理页面地址是 `/admin`。

所有 `/v1` 接口支持 `Authorization: Bearer <token>` 或
`X-API-Key: <token>`。设备上传使用 `CHILDCARE_DEVICE_TOKEN` 和
`X-Device-Id`；管理写操作使用管理 token 或管理页面会话；Hermes 使用
`CHILDCARE_HERMES_TOKEN`；外部读取/导出使用公开读取或 Hermes token。事件
`id` 是设备侧幂等键，同一批记录重试不会重复写入。事件类型与固件一致：
`feeding`、`diaper`、`sleep`、`bath`、`tummy` 和 `timer`。每条事件都有
`occurred_at`，吃奶记录因此同时包含准确时间点、`amount_ml` 和 `ingredient`。

Hermes 使用 `GET /v1/hermes/children/{child_id}/analysis-input` 读取稳定的
`schema=xigua-childcare-export-v1` 数据，也可以用
`POST /v1/hermes/audio/tracks` 补充音频目录元数据，但不能写入育儿事件。
如需补充实际音频文件，可向 `PUT /v1/hermes/audio/files/{file_name}` 上传有
大小限制的音频字节，再登记目录项。返回的 `audio_tracks` 只含元数据和播放地址，
不含服务器凭据或数据库路径。

第一版客户端流程：

1. 用 `POST /v1/children` 创建孩子。
2. 用 `POST /v1/devices/register` 注册设备。
3. 分批上传设备 NVS 中的本地记录。
4. 从 `/v1/children/{id}/summary` 和 `/events` 给手机或后台展示。
5. 查询 `/v1/audio/tracks?category=white_noise`，播放返回的地址。

## 部署准备（2026-10-01）

已调查服务器为 Ubuntu 22.04.4 LTS、x86_64，Python 3.10.12、Docker 27.0.3、
Compose 2.28.1。终端以 root 身份运行，当前目录 `/root`，约有 44 GB 可用磁盘和
4.7 GiB 可用内存。已有 1Panel、OpenResty、PostgreSQL 及其他应用容器，80、443、
8090 端口已使用，8000 未监听。在 `/opt`、`/srv`、`/root`、`/home` 四层目录内
未找到 Git checkout；这个有限范围的搜索不排除其他位置存在 checkout。

建议使用独立 checkout，例如 `/opt/ai-passport`（尚未创建）。Compose 仅绑定
`127.0.0.1:8000`，由现有 OpenResty 将选定 HTTPS 域名转发至该地址。Python 在
3.12 容器内运行。部署前需确定域名、DNS、TLS，在 `.env` 配置角色令牌、密码和
会话密钥，并确认代理能访问主机回环地址；已确认 OpenResty 容器使用 `host` 网络，可使用该回环上游。站点配置挂载自
`/opt/1panel/www/conf.d`，应通过 1Panel 管理。
不要复用已占用的应用端口。

本次调查未修改服务器文件、服务或防火墙规则。部署、远程镜像构建或拉取、代理配置、
备份与线上验收仍待完成。升级前通过 SQLite 备份 API 保证 WAL 一致性，并保留
媒体文件；不要删除数据卷。

每日汇总按儿童的 IANA 时区计算，包含当地零点、排除次日零点。无效时区和日期会
被拒绝。设备令牌可以读取音频列表与单曲，不能读取儿童事件或分析导出。

使用 Python 3.12，后端回归测试可独立于固件工具链运行：

```bash
python -m pip install -r backend/requirements.txt pytest httpx
python -m pytest backend/tests -q
```

合并部署与兼容迁移见[部署指南](deploy/README.zh_CN.md)。

## 单宝宝工作台

统一登录沿用原 Cloud Backup 管理 Key。默认选定一个宝宝和一台设备，无需注册。工作台包含喂养与睡眠记录、每日图表、完整 CSV/JSON 导出、声音管理、备份文件及历史版本、应用密钥和完整归档；Hermes 使用独立的已配置 Skill 下载。

登录 Key 输入框使用明文文本，便于手机输入法输入和粘贴，并关闭自动大写与
自动纠错。提交时只去掉首尾空白，仍须使用完整的原管理 Key。

固件上传带版本号的 32 条 NVS 环形记录快照，稳定序号支持重试、重启和撤销。环形覆盖后，服务器仍保留已经上传的历史记录。旧固件已覆盖的记录无法恢复，长期离线超过容量会丢失尚未同步的记录。未校准时间的记录保留，但不参加日期统计。睡眠按当地午夜拆分。

从工作台下载设备配置至 `main/xigua_backend_config_local.h` 后构建。声音要求 12 kHz 单声道 16 位 PCM WAV，最大 25 MB。播放状态来自设备实际回执。

删除记录会转入可恢复存储，不再参与统计或导出；设备和手工请求重试不会自动恢复已删除内容。可在记录页恢复单条数据。预置儿歌保留作者、来源、许可和格式转换说明。

儿歌设备控制支持播放、暂停、继续与停止。暂停保留播放位置并释放音频占用；开始 AI、录音或其他音频流程会结束儿歌会话。设备命令回执新增 `paused`，工作台会区分请求已发送和设备已执行。

## 结合记录的助手与照护交接

设备鉴权接口 `GET /v1/device/context` 返回有界的
`xigua-care-context-v1` 摘要：当前快照版本、生成时间、按当地日历计算的
七天统计、最近已知时间的喂养／睡眠／尿便和家长留言。设备令牌只读取配对
家庭的摘要，不获得任意孩子历史记录的读取权限。时间未知记录单独计数，
不参加按日期的统计。

工作台的照护交接页使用 `GET /admin/api/care`。管理员通过
`PUT /admin/api/handoff` 和 `{"note":"..."}` 保存或清空持久留言，最多160字。
留言按纯文本显示，交给助手时也只作为数据，不作为设备命令。较新的设备
快照可原位修正喂养奶量、时间和食材，不新增事件，旧版本重试也不能覆盖修正。
需同时升级后端与固件，才能使用近七天上下文和云端留言。单次提醒在设备上
创建、管理和持久保存；本轮没有手机通知服务或提醒调度接口。

## 安卓蓝牙 Wi-Fi 配网

工作台的“手机蓝牙配网”页（`/console#phone`）直接连接应用 BLE 服务。
先在设备进入“Wi-Fi配网 → 手机蓝牙配网”，再用安卓 Chrome 选择附近的
`Xigua-` 设备，并在系统配对框输入设备屏幕显示的六位数字。每次设备窗口
最长五分钟；离开设备配网页或结束窗口会停止 BLE，恢复 AI 与云端任务。
NFC 负责打开页面；无源标签不能开启设备的配网窗口。

手机可扫描 2.4 GHz 网络、填写隐藏 SSID 与密码，或选择已有内置网络。
内置密码留在设备上。提交的密码只通过已鉴权的 BLE 发送，随后清空输入框，
不会保存到浏览器或上传后端。新网络拿到 IP 后才持久保存，失败不覆盖上次
保存的配置。本轮不包含 API Key／模型编辑或 iPhone 配网。

线上入口：`https://162.14.108.234/cloud-backup/console#phone`。原 HTTP NFC
登录地址自动转入 HTTPS，无需重写标签。可选环境变量 `XIGUA_PHONE_HTTPS_URL`
指定正式 HTTPS 工作台地址。反向代理须保留 `X-Forwarded-Proto: https`，
Uvicorn 只信任该代理的私网来源。原设备／备份 HTTP 协议保持兼容。
蓝牙脚本为需登录的 `/console/api/bluetooth.js` 资源，不含管理 Key。

运行 `node tests/test_xigua_ble_client.js` 检查实际网页客户端的分包、回执、
断线重连和工作台脚本。仓库门禁包含 `tests/test_xigua_ble.py`；真实安卓配对
与 Wi-Fi 保存仍需设备验收。
