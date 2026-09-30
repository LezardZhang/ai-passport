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
