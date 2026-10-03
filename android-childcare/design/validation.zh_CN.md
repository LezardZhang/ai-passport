<p align="right"><strong>简体中文</strong> · <a href="validation.md">English</a></p>

# 设计验证记录

## 第二版：统一照护与家庭运作

2026-10-03，Asia/Shanghai。本次内置浏览器不可用，改用已连接的 Chrome 实际检查原型。[第二版规范](simplified-design.zh_CN.md)取代原四入口原型。下方第一版交互表只保留为历史证据。

| 检查 | 观察结果 |
| --- | --- |
| 导航 | 照护、记录、家庭三个主入口；导出、资料、维护为次级页面 |
| 手机基线 | 390 × 844；页面宽与滚动宽均为 390 px，没有横向溢出 |
| 输入可达 | 语音与发送高 48 px，底部 y=712.5；导航范围 y=761–829；对话内容独立滚动 |
| 本机人物登记 | 填写示例称呼、预览本机身份、模拟失败、重试保存；刷新后保留演示人物 |
| 最终修改后的失败状态 | 预览内显示持久失败原因，保留草稿及保存/返回修改；没有新增人物 |
| 人物更新/停用 | 更新显示资料版本 2；停用清空当前记录人，已有事件仍保留原记录人称呼 |
| 统一记录路由 | 150 ml 短句生成确认卡；确认后新增一条本机演示记录，记录页合计为 150 ml/一次喂奶 |
| 故事不可用 | 原主题保留在所属卡片，显示服务未连接；没有线上请求或假 AI 回复 |
| 统一语音 | 开始/停止模拟产生可编辑文字；填入后仍需发送，没有自动新增记录 |
| 麦克风拒绝 | 权限拒绝模拟提供系统设置与文字输入 |
| 夜间/130% 大字 | 实际浏览器截图可见文案换行，语音/发送与三个主导航仍可见 |
| 导出与维护 | 记录导出可打开；示例安装版本、未配置更新渠道、备份/恢复限制明确说明 |
| 浏览器控制台 | 最终检查无错误条目 |

第二版截图：

- 统一照护（本地产物：`screenshots/unified-care-v2.jpg`）与桌面总览（本地产物：`screenshots/unified-desktop-v2.jpg`）
- 意图确认（本地产物：`screenshots/intent-preview-v2.jpg`）与记录（本地产物：`screenshots/records-v2.jpg`）
- 家庭（本地产物：`screenshots/household-v2.jpg`）与人物确认（本地产物：`screenshots/person-review-v2.jpg`）
- 版本与维护（本地产物：`screenshots/maintenance-v2.jpg`）
- 故事不可用（本地产物：`screenshots/service-unavailable-v2.jpg`）
- 夜间/大字（本地产物：`screenshots/unified-night-large-v2.jpg`）与语音文字确认（本地产物：`screenshots/unified-voice-review-v2.jpg`）

人物持久化仅对演示浏览器数据真实生效。照护记录、语音、播放、提醒、同步、升级与恢复仍为模拟或明确不可用的交接。本报告不宣称第二版已在原生 APK 实现。真实存储空间耗尽、Android 迁移/进程回收和真机行为仍需实现验收。

## 第一版：历史原型检查

日期：2026-10-02，Asia/Shanghai。在 Codex 内置浏览器测试真实本地原型。此验证针对设计产物，不等于原生 APK 或线上后端通过。

| 检查 | 观察结果 |
| --- | --- |
| JavaScript 语法 | PASS：`node --check prototype.js` |
| 设计 token | PASS：JSON 解析成功 |
| Markdown 配对与链接 | PASS：中英配对及所有本地目标存在 |
| 浏览器控制台 | 最终检查没有错误条目 |
| 手机基线 | 390 × 844，页面无横向溢出，底部导航可见 |
| 喂奶保存/撤销 | 180 ml 示例保存后显示本机已存/待同步；撤销移除对应事件，并显示删除操作待同步 |
| 语音草稿 | 开始/停止模拟后得到可编辑文字；填入问答框后仍需主动发送 |
| 问答/故事归属 | 两个专属主题切换后仍保留在各自结果页 |
| AI 失败/重试/取消 | 失败保留问题，重试进入处理中，取消回输入 |
| 麦克风拒绝 | 显示系统设置与改用文字 |
| 跨页播放器 | 故事标题跟随导航留在播放条，检查了暂停/继续状态与控件 |
| 首次空档案 | 零记录，无活动睡眠/播放，生日未知，交接/提醒为空 |
| 夜间/130% 大字 | 390 px 手机内快捷入口转为 350 px 单列，无横向溢出；导航底部 y=829，位于 844 px 视口内 |

截图：

- 首页（本地产物：`screenshots/home-mobile.jpg`）与桌面总览（本地产物：`screenshots/home-desktop.jpg`）
- 喂奶表单（本地产物：`screenshots/feeding-sheet.jpg`）
- 录音状态（本地产物：`screenshots/voice-recording.jpg`）
- 故事结果（本地产物：`screenshots/story-result.jpg`）
- 历史（本地产物：`screenshots/history-mobile.jpg`）
- 首次空状态（本地产物：`screenshots/first-launch-empty.jpg`）
- 夜间/大字（本地产物：`screenshots/night-large-type.jpg`）

Build: NOT RUN（设计原型无需编译；APK 构建归主实现流程）。

Host tests: NOT RUN（执行了语法/JSON/链接静态检查及浏览器交互检查；本设计代理没有执行 Android 状态机测试）。

Device tests: NOT RUN。

原生截图证据更新（2026-10-03）：已审阅提供的模拟器 200% 首页、基线首页/历史、夜间表单与紧凑播放条，见[最终原生截图审阅](native-ui-review.zh_CN.md)。

Unverified: 本设计审阅尚未验证真实手机排版/行为、其他页面 200% 布局、TalkBack、键盘弹出布局、真实 ASR/TTS、线上后端、通知/重启、音质与后台媒体生命周期。原型睡眠/语音/AI/播放为状态模拟。全仓库 gate 和 APK 验证由主流程负责。
