<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# 源码检出的配置准备

本次 Git 同步包含空凭据模板。现有私人 code13 APK 保留主人授权的配置，安装包字节、签名及云端发布均未改变。本提交不包含真实提供方、下载或业务配置、发布凭据、签名文件和构建产物。

在其他机器构建前，将三个应用模板复制到 `app/src/main/assets/`，去掉 `.example` 后缀，再从主人私人配置中填写已授权的提供方密钥及专用下载 Token。家庭 Token 必须是此前授权的 `PERSONAL_CHILDCARE_TOKEN`，它的缺失仍然是真实同步限制。将发布器模板复制到 `.local/update-publisher.json`，配置不同的发布/下载 Token 及原签名证书 SHA256。构建兼容更新时保留原 `.local/signing/childcare-local.jks`，不得为家庭覆盖升级另生成签名。

```sh
mkdir -p app/src/main/assets .local
cp config/mimo-config.example.json app/src/main/assets/mimo-config.json
cp config/update-config.example.json app/src/main/assets/update-config.json
cp config/family-cloud-config.example.json app/src/main/assets/family-cloud-config.json
cp config/update-publisher.example.json .local/update-publisher.json
```

空 Token 是有意保留的占位符，不是可用凭据。家庭数据接口与独立 APK 更新服务分离，旧更新地址仅为兼容别名。普通构建不会发布版本；[发布操作与长期授权](../docs/cloud-updates.zh_CN.md)说明既有范围。

历史产物和未选入的截图仍保留本机。Git 包含应用源码、工具、测试、配对文档及选定的当前脱敏回执。文档中标记为本地产物的引用，不表示其二进制文件或界面捕获已发布到 Git。
