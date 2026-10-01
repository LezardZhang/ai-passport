<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# 合并文件备份与育儿服务

统一镜像仅替换现有 `cloud-backup` 服务，保留 Cloud Backup 0.3.2 的认证、迁移、
版本对象、配额、恢复工具和专属 Skill 下载。两个 FastAPI 应用运行在同一进程，
启动、维护和关闭均正常传递，各自保留数据库和凭据模型。这是服务与入口整合，
不把育儿记录转换成文件对象。

## 兼容入口

现有 Cockpit Nginx 去掉 `/cloud-backup/` 前缀后转发到 `cloud-backup:8080`，
无需修改端口映射或 Nginx：

| 现有主机与端口后的外部路径 | 用途 |
| --- | --- |
| `/cloud-backup/api/v1/...` | 现有文件 API，保持不变 |
| `/cloud-backup/admin/v1/...` | 现有备份管理与 Skill 导出，保持不变 |
| `/cloud-backup/admin/` | 原备份管理台，保持不变 |
| `/cloud-backup/console` | 统一服务导航 |
| `/cloud-backup/childcare/admin/login` | 育儿管理台与配置化 Skill 下载 |
| `/cloud-backup/childcare/v1/...` | 育儿 API |
| `/cloud-backup/childcare/media/...` | 育儿音频 |

Cockpit 在 `/cloud-backup/` 外的 `/v1/` 不受影响。不让第二个容器绑定宿主机
443，也不新增后端公网端口。已确认 32070 当前使用 HTTP；生产凭据传输需要已有
可信网络或 HTTPS 入口。此次整合无需修改受保护的 docforge、1Panel、DNF 或 Mihomo。

## 源码与构建

使用用户独立仓库 `LezardZhang/cloud-backup-cockpit-addon`，版本 0.3.2，本地提交
`311656ed23622edd9f240f14c651fd4004cb2361`。SSH 认证失败，尚未确认远端是否有更新。
镜像除了 Python 模块，还必须包含迁移文件、管理页面、协议、Skill 资源和 Linux
x86_64/CPython 3.12 wheelhouse。统一 Dockerfile 沿用原固定 Python 镜像和离线
哈希依赖，不修改或复制维护 Cloud Backup checkout。

在私有 Compose 环境中补充：

```text
CHILDCARE_SOURCE_DIR=/absolute/path/ai-passport/backend
CLOUDBACKUP_SOURCE_DIR=/absolute/path/cloud-backup-source
CHILDCARE_ENV_FILE=/absolute/path/childcare.env
CHILDCARE_MEDIA_PATH=/absolute/path/childcare-media
```

育儿环境文件参考 `../.env.example`，公开地址必须以 `/cloud-backup/childcare`
结尾。统一服务缺少设备、管理、Hermes、只读令牌或管理密码时拒绝启动。保留原备份
环境，尤其是 `CLOUDBACKUP_KEY_ENCRYPTION_KEY`、管理员令牌、公开地址和
`CLOUDBACKUP_ROOT_PATH=/cloud-backup`。原配置挂载目录容纳两个独立数据库，
育儿媒体使用单独挂载，需让原容器 UID/GID（通常 10001:10001）可写。
不要用全新目录覆盖已有数据库或媒体目录。

将 `unified.override.yml` 放在现有 Cockpit 和备份 Compose 文件的**最后**，
保留全部原环境文件。先检查 `docker compose config --quiet` 并构建候选
`cloud-backup` 镜像。容器仍监听 8080。完成备份和验证后，只使用
`up -d --no-deps cloud-backup` 重建该服务，保留其他服务。上游服务别名不变，
覆盖文件默认 `PERSONAL_SERVICES_VERIFY_ONLY=1`：验收期间关闭备份维护，并拒绝
GET/HEAD/OPTIONS 之外的请求，包括登录；可用管理 Bearer 令牌验证 Skill 下载。
清单一致后设置为 `0`，仅重建该服务以恢复正常运行。原 `depends_on` 关系不要求重建面板。

尚未执行 Docker/Compose 构建或线上切换。本机无可用 Docker daemon，API 测试
不能当作镜像验收。

## 原数据与回退

已调查服务器将 `/home/clouddata/config` 挂到 `/config`，将
`/home/clouddata/data` 挂到 `/data`。2026-10-01 检查时有 3 个用户、5 个密钥、
59 个文件行、521 个版本行，SQLite quick_check 为 `ok`。这是时点基线，
不保证后台写入者持续空闲。

1. 最终对比前暂停客户端、定时写入与维护。通过原备份管理 API 创建在线一致备份，
   下载并用原恢复工具校验。私下保留旧镜像 ID、Compose/环境文件和加密主密钥。
   不要分别复制活跃 SQLite 数据库及 WAL 文件。
2. 对暂停的原数据库与对象目录运行清单工具：
   `python check_existing_data.py /home/clouddata/config/cloud-backup.db /home/clouddata/data > before.json`。
   工具在 SQLite 事务中读数据、检查完整性、计算全部活跃对象及元数据哈希，
   不输出记录值或凭据。
3. 候选配置沿用这些目录和密钥。数据库 ID、文件路径、密钥哈希/密文与版本标识
   原位保留，不转换原备份数据。若育儿后台已有数据，另外创建 SQLite 在线快照并
   保留媒体，在停止写入后把快照恢复到新 `childcare.db` 路径；不得覆盖已有目标。
   空育儿数据库仅适用于从未部署过育儿服务的首次上线。
4. 验证 `/readyz`、旧密钥认证、当前和历史下载、原专属 Skill 导出与新育儿流程。
   恢复写入前使用 `--compare before.json` 再次对比清单。元数据指纹排除密钥 last_used_at，避免验收读取造成假差异；其他维护时间戳可能
   合理改变元数据，应调查差异，不能强行匹配或删除数据。
5. 回退时恢复旧镜像和配置，使用相同备份目录和加密主密钥，仅重建 `cloud-backup`。
   回退期间保留育儿数据库与媒体。不得使用 `down -v`、删除数据卷或在写入活跃时
   恢复备份。若文件受损，继续使用原 Cloud Backup 恢复工具处理已验证的独立备份。

回归测试先在原服务创建密钥和两个文件版本，再验证合并、原/新 Skill 导出、带前缀
及代理去前缀请求、损坏对象拒绝、切回原服务读取。这些测试不代表线上数据已迁移。

## 配置化 Skills

Cloud Backup 原按密钥下载 Skill ZIP 的接口保持不变。育儿新增经过管理认证的
`GET /admin/api/skills/{hermes|public|device}`（在育儿前缀下），管理台提供下载按钮。
ZIP 包含 `SKILL.md`、配置好的 `connection.json` 和无需第三方依赖的 Python GET
助手。把选定目录解压到 Agent 的 skills 目录即可使用已有地址和令牌。
ZIP 响应使用 `Cache-Control: no-store`，下载包等同于凭据，不得发布。
育儿目前每个角色共用一个令牌，轮换会撤销该角色全部下载包，不能单独撤销某次下载。
助手拒绝重定向，避免把凭据送往其他入口。

## 运动模块边界

确认运动 Android 客户端的实际仓库、数据格式和账户/前缀归属之前，可继续使用原文件
API。不能仅凭文件名关键字推断归属，也不能把全部备份用户迁入一个运动账户。
后续 `/fitness/` 模块应有独立数据库与 schema 版本，导入器需验证原数据哈希、记录
源版本 ID、支持幂等。导入后仍保留原文件版本。APK 地址/协议改造与配套配置化
Skill 需针对已确认客户端测试。目前等待项目确认。

## 验证

安装 Cloud Backup 依赖，并将其源码加入 PYTHONPATH：

```sh
PYTHONPATH=/absolute/cloud-backup/src python -m pytest ../tests -q
PYTHONPATH=/absolute/cloud-backup/src python -m pytest /absolute/cloud-backup/tests -q
```

在本目录运行，使用 Python 3.12。缺少 Cloud Backup 时合并测试会跳过；跳过不能
算作兼容验证通过。

## 基于服务器现有镜像部署

`install_existing.py` 只操作已确认的 Cockpit `cloud-backup` 服务。它检查现有
源码哈希，通过 `Dockerfile.installed` 扩展固定镜像，保留经过验证的一致性备份，
再在只读切换期间对比元数据及全部活跃对象哈希。原服务和育儿服务的 Skill 导出
均通过后才恢复正常运行；切换失败则恢复原镜像。其他容器和宿主机端口映射不变。

发布目录包含私密的 `childcare.env`、部署证据、`service-compose.sh` 和
`rollback.sh`。使用 `sh service-compose.sh ps` 查看服务，使用 `sh rollback.sh`
恢复原镜像。需保留此目录及配置；后续重建服务必须带上 `installed.override.json`。
脚本拒绝已有育儿数据库，不是通用迁移工具，也不能作为可重复运行的更新器。


2026-10-01 第二版已部署，只重建 cloud-backup。原数据库和对象清单校验通过，其他容器不变。私有检查点和回滚脚本保存在 /home/clouddata/releases/personal-services-20261001-v2。旧管理页面跳转到统一工作台。

原 `/cloud-backup/admin/` 与育儿登录页面均跳转至统一工作台或统一登录。`/cloud-backup/console` 提供完整育儿和备份功能。
