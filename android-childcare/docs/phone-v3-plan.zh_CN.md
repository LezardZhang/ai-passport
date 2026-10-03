<p align="right"><strong>简体中文</strong> · <a href="phone-v3-plan.md">English</a></p>

# 安卓手机第三版实施计划

> **执行代理：** 用superpowers:executing-plans在当前会话实施，最终独立评审一次；设计代理仅负责design/。

**目标：** 修正语音提交生命周期，按安卓重构日常模块。
**架构：** 独立SpeechSession、保留原生视图的CareComposer、可注入SpeechBackend、前台AudioRecord、长WAV流式分段及私人重试草稿；受跟踪配置提供服务默认值。保留SQLite版本3及媒体/同步所有者。
**技术：** Java/Android API26-35、SQLite、WAV、HTTPS、系统媒体/权限/文档/安装API。
**设计：** [第三版设计](phone-v3-design.zh_CN.md)。

## 约束及评审重点
只改android-childcare，保留包名/签名/数据。证据不含原始密钥，不发布生产。主人明确允许内置适用密钥。发送不得取消自己正在转写的内容，刷新不得替换录音控件，旧回调不得提交，失败保留重试草稿，长语音始终流式且有资源约束。

## 任务
- [x] 主机先失败：SpeechSession录音/转写中发送、重复/迟到/失败/取消；SpeechAudio超过60秒的长WAV分段、头/对齐及内存边界。实施独立规则并运行主机测试。
- [x] 替换VoiceInput/MicrophoneCapture生命周期，增加SpeechBackend/Providers/SpeechDrafts；录音前验证提供方，去掉固定中断并支持串行文件分段/重试；安卓测试权限/取消/退后台。
- [x] 增加CareComposer并保留语音期间视图，MainActivity仅分流完整文字。去掉旧独立语音表单及日常传输诊断，保留有意的多服务设置，按设计重构家庭/服务/更新。
- [x] 内置适用提供方及可选云/发布默认配置，不包含Wi-Fi凭据；不显示值地检查存在/鉴权。匹配Token Plan已鉴权；真实Android模型/对话/合成ASR通过。
- [x] 构建code3/0.3.0-local，更新code4合成升级夹具，跑主机/Android/原生UI/完整门禁及最终评审。同签名覆盖手机不清数据，检查真机页面，凭据可用后验证真实语音；更新双语交付/资源预算及精确归档。

## 决策记录
复用现有android-childcare目录：应用在这里尚未跟踪，另一个聊天负责后端，新工作树会遗漏当前源码；保留无关变更。
不增加重复设计/计划批准或提交门禁：主人已有自主实施授权，现在明确要求重构。
录音没有固定时长中断，但保留有限私人文件/磁盘储备以防耗尽空间；长ASR串行分段。
按主人要求跟踪并打包私人凭据；最新Token Plan匹配指令已解除先前普通密钥依赖。

最新决策：主人明确要求Token Plan匹配默认配置、多URL/Key档案及模型发现；家庭保留这些有意设置，对话/语音独立选择，并提供手填降级。

验收：主机111、Android354、真实模型/对话/合成ASR，真机覆盖安装保留数据；真机页面/网络及余项见交付说明。
