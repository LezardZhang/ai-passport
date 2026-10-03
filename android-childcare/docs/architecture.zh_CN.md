<p align="right"><strong>简体中文</strong> · <a href="architecture.md">English</a></p>

# Android 第四版架构

[设计](phone-v3-design.zh_CN.md)及[计划](phone-v3-plan.zh_CN.md)。原生照护/记录/家庭使用保留视图的CareComposer；主机测试的IntentRouter/CareRules生成确认候选，不自动写记录。

SpeechSession管理阶段/代次/待发送。VoiceInput在录音前验证冻结配置，管理权限和前台取消，拒绝旧回调。MicrophoneCapture流式写AudioRecord PCM，联网前释放录音器，完成但识别失败的WAV由SpeechDrafts保留。SpeechAudio顺序生成有界WAV段。单语音线程防止麦克风/识别并发；退后台保留已完成草稿，明确取消才删除。

Providers在版本3元数据保存多套服务和独立对话/语音绑定；配置修订隔离编辑/删除后的异步模型发现，GET /models列表单独缓存。请求存储提供方快照和传输归属；AiEngine处理直接请求，备份Api不得领取或替换用户所选服务。旧后端任务保持服务端归属；直接生成中断显示可重试。

Store版本3保留人物状态、历史姓名快照、记录/队列UUID、对话、提醒和音频。带数据版本2升级保留记录及操作身份。新备份协议在操作/编辑创建时保存服务UUID、档案UUID及Base URL；旧记录编辑/删除/结束睡眠继承原身份。Api无旧头发现能力，此后读取/写入均使用冻结身份；旧队列身份缺失或变化会隔离，不自动重绑。拉取页以事务应用，保护本地待提交写入；清空代次不能复活旧记录。家庭联系方式留在本机；有界照护者 ID/姓名快照通过可选 `data.caregiver` 同步，须服务声明 `childcare_caregiver`。旧服务不支持时保留署名记录待同步。冻结旧请求保持原样，原子确认后用新操作补齐归属。远端 null 清空、省略保留；冲突保留本地也保留明确归属，包括清空姓名。

单数据库线程串行写入，单网络线程串行同步/对话/模型/更新；独立语音线程最多允许两个应用HTTP请求。原生播放/朗读共享一个前台播放器/会话。更新流式处理有界文件，校验SHA256、包名、版本、系统及原签名，通过只读URI授权给Android确认安装。尚无真实备份凭据或发布频道。[资源预算](resource-budget.zh_CN.md)与[交付](delivery.zh_CN.md)区分测量证据和规划边界。

[时间线架构与验收](caregiver-timeline.zh_CN.md)：原生连续竖轴、每页100条、实际照护者选择与旋转草稿保留；管理端纯文本卡片、按天快照分页与简明表单。只改归属时保留未知/自定义资料及独立保存的睡眠时长。
