/* Interface simulation only. No network, microphone, notifications, or real audio. */
const $ = s => document.querySelector(s);
const icon = (name, cls='') => `<i data-lucide="${name}"${cls ? ` class="${cls}"` : ''}></i>`;
const esc = value => String(value).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const state = {
  page:'home', night:false, large:false, offline:false, permission:false, failNext:false, empty:false,
  sleep:true, feedAmount:150, milkKind:'配方奶', diaperKind:'尿尿', filter:'全部', range:'今天', category:'全部',
  pending:2, exportFormat:'CSV', reminders:[{title:'准备晚间奶',time:'22:30',done:false}],
  events:[{id:1,kind:'feed',time:'19:40',title:'喂奶 · 150 ml',note:'配方奶',pending:false},{id:2,kind:'diaper',time:'18:55',title:'换尿布 · 尿尿',note:'无备注',pending:true},{id:3,kind:'sleep',time:'16:20',title:'睡眠 · 1 小时 12 分',note:'15:08 – 16:20',pending:false,minutes:72},{id:4,kind:'feed',time:'15:00',title:'喂奶 · 180 ml',note:'配方奶',pending:true},{id:5,kind:'feed',time:'11:30',title:'喂奶 · 150 ml',note:'母乳瓶喂',pending:false},{id:6,kind:'diaper',time:'10:20',title:'换尿布 · 尿尿',note:'无备注',pending:false},{id:7,kind:'diaper',time:'09:40',title:'换尿布 · 便便',note:'无备注',pending:false},{id:8,kind:'diaper',time:'08:30',title:'换尿布 · 尿尿',note:'无备注',pending:false},{id:9,kind:'diaper',time:'07:05',title:'换尿布 · 尿尿',note:'无备注',pending:false},{id:10,kind:'sleep',time:'06:50',title:'睡眠 · 9 小时 35 分',note:'昨天 21:15 – 今天 06:50',pending:false,minutes:410}],
  sheet:null, voiceMode:'ask', voiceState:'idle', player:null, playback:false,
  ask:{status:'idle',draft:'',prompt:'',result:'',requestId:0}, story:{status:'idle',draft:'',prompt:'',result:'',requestId:0},
  handoffNote:'晚间奶用配方奶。宝宝今天精神很好，入睡前喜欢轻轻拍拍。',
  lastUndo:null, lastSelected:null, toastTimer:null, sleepStart:'20:58', sleepMinutes:42, downloaded:new Set()
};
const sampleEvents=state.events.map(e=>({...e}));
const legacyPages=[['home','今日','house'],['history','记录与统计','chart-no-axes-column-increasing'],['sleep','睡眠','moon'],['companion','陪伴','sparkles'],['ask','语音问答','message-circle'],['story','讲故事','book-open'],['library','音频库','headphones'],['handoff','照护交接','heart-handshake'],['reminders','提醒','bell'],['settings','我的与设置','settings'],['export','导出','download']];
const annotations={
  home:['先看见宝宝的状态','活动中的睡眠优先出现。三项快捷记录和语音入口降低抱着宝宝时的操作负担；云端状态与本地保存分开表达。'],
  history:['同一份记录，多个视角','今日、时间范围统计和历史列表共享数据来源。待同步数量与临时统计明确标记；每条记录都能编辑、删除或恢复。'],
  sleep:['计时跟着生活继续','离开页面不结束睡眠。结束时一次保存开始与结束时间；补记用系统日期时间控件，跨午夜无需手动拆段。'],
  companion:['问答、故事、音乐各有归属','三个明确入口。问答是互动，故事是阅读与朗读，音频库是浏览与播放；迟到结果只回到自己的请求。'],
  ask:['先看清，再发送','点击录音后有明确状态，停止后保留文字供系统输入法修正。录音不会直接保存记录或发送问题。'],
  story:['文字先到，朗读随时开始','故事有独立输入、独立结果和独立重试。播放能暂停、继续、停止；换页不会丢掉当前故事。'],
  library:['把声音交给手机','按有效分类浏览，搜索和下载可达。一个分类失败不隐藏其他分类；播放条和系统媒体控制使用同一状态。'],
  handoff:['把下一班照护说清楚','最新记录、正在进行的睡眠、已同步时间和家长留言分别呈现。缺少记录只显示暂无，不推断活动没有发生。'],
  reminders:['提醒完成一件事','自定义时间和内容，不生成喂养建议。完成、延后十分钟和编辑都清楚；通知权限状态会影响可达性。'],
  settings:['家庭自用，轻装开始','默认宝宝资料，无注册。连接、同步、麦克风、通知、外观、缓存和导出都有可检查状态。'],
  export:['时间范围和数据状态都说清','默认导出本机完整记录，含待同步标记；也可只导出已同步记录。文件交由系统分享菜单处理。'],
  sync:['本地保存不等于云端完成','待同步操作逐项可见；重试保留操作身份，冲突需要查看版本后处理，不靠手机时钟静默覆盖。'],
  profile:['先初始化，再慢慢补充','首次启动直接进入首页，昵称可修改。生日未知时不猜年龄；更改时区会说明统计日界线。'],
  player:['播放操作在拇指附近','大暂停按钮、跳转、停止和睡眠定时器。正文仍能阅读；系统通知与应用内播放条体现同一状态。']
};
function legacyInitialize(){
  $('#studio-pages').innerHTML=pages.map(([p,label,i])=>`<button data-page="${p}" class="${p===state.page?'active':''}">${icon(i)}${label}</button>`).join('');
  $('#bottom-nav').innerHTML=[['home','今日','house'],['history','记录','chart-no-axes-column-increasing'],['companion','陪伴','sparkles'],['settings','我的','user-round']].map(([p,label,i])=>`<button data-page="${p}"><span class="nav-icon">${icon(i)}</span>${label}</button>`).join('');
  document.addEventListener('click',onClick);
  $('#offline-demo').addEventListener('change',e=>{state.offline=e.target.checked;render();});
  $('#permission-demo').addEventListener('change',e=>{state.permission=e.target.checked;});
  $('#failure-demo').addEventListener('change',e=>{state.failNext=e.target.checked;});
  $('#empty-demo').addEventListener('change',e=>{state.empty=e.target.checked;state.sleep=!state.empty;state.pending=state.empty?0:2;state.events=state.empty?[]:sampleEvents.map(e=>({...e}));if(state.empty){state.player=null;state.playback=false;state.handoffNote='';state.reminders=[];state.ask={status:'idle',draft:'',prompt:'',result:'',requestId:state.ask.requestId+1};state.story={status:'idle',draft:'',prompt:'',result:'',requestId:state.story.requestId+1};}$('#toast').classList.remove('show');render();});
  $('#large-demo').addEventListener('change',e=>{state.large=e.target.checked;$('#phone').classList.toggle('large-type',state.large);render();});
  document.addEventListener('input',e=>{if(e.target.id==='question-input')state.ask.draft=e.target.value;if(e.target.id==='story-input')state.story.draft=e.target.value;if(e.target.id==='library-search')filterTracks(e.target.value);});
  render();
}
function icons(){if(window.lucide)lucide.createIcons({attrs:{'stroke-width':1.8,'aria-hidden':'true'}});}
function legacyNavGroup(){if(['ask','story','library','player'].includes(state.page))return 'companion';if(['sleep','export'].includes(state.page))return 'history';if(['sync','profile'].includes(state.page))return 'settings';if(['handoff','reminders'].includes(state.page))return 'home';return state.page;}
function go(page){state.page=page;state.sheet=null;$('#overlay').innerHTML='';render();$('#screen').scrollTop=0;}
function header(title,subtitle='',action='settings',back=false){return `<div class="header-top"><div>${back?`<div class="back-title"><button class="icon-button" data-action="back" aria-label="返回">${icon('arrow-left')}</button><h1 class="header-title">${title}</h1></div>`:`<h1 class="header-title">${title}</h1>`}${subtitle?`<div class="header-subtitle">${subtitle}</div>`:''}</div>${action==='profile'?'<button class="profile-button" data-page="profile" aria-label="宝宝资料">瓜</button>':action==='add'?`<button class="icon-button" data-action="record-menu" aria-label="新增记录">${icon('plus')}</button>`:action==='none'?'':`<button class="icon-button" data-page="${action}" aria-label="${action==='reminders'?'提醒':'设置'}">${icon(action==='reminders'?'bell':'settings-2')}</button>`}</div>`;}
function syncText(){return `<button class="sync-indicator ${state.pending?'pending':''}" data-page="sync">${icon(state.offline?'cloud-off':state.pending?'refresh-cw':'cloud-check')} ${state.offline?'离线，记录仍可保存':state.pending?`${state.pending} 项待同步`:'本机示例 · 未连接服务'}</button>`;}
function section(title,right='',page=''){return `<div class="section-heading"><h2>${title}</h2>${right?`<button class="text-button" data-page="${page}">${right}${icon('chevron-right')}</button>`:''}</div>`;}
function notice(text,type='',i='info'){return `<div class="notice ${type}">${icon(i)}<span>${text}</span></div>`;}
function empty(title,detail,button='',action='record-menu',i='notebook-pen'){return `<div class="empty"><div class="empty-icon">${icon(i)}</div><b>${title}</b><p>${detail}</p>${button?`<button class="secondary-button" data-action="${action}">${button}</button>`:''}</div>`;}
function legacyRender(){
  const p=state.page;
  $('#phone').classList.toggle('night',state.night);
  $('#phone').classList.toggle('large-type',state.large);
  document.querySelectorAll('.theme-button span').forEach(el=>el.textContent=state.night?'日间模式':'夜间模式');
  $('#studio-pages').querySelectorAll('[data-page]').forEach(el=>el.classList.toggle('active',el.dataset.page===p));
  $('#bottom-nav').querySelectorAll('[data-page]').forEach(el=>el.classList.toggle('active',el.dataset.page===navGroup()));
  const ann=annotations[p]||annotations.home;$('#design-heading').textContent=ann[0];$('#design-description').textContent=ann[1];
  const views={home:homeView,history:historyView,sleep:sleepView,companion:companionView,ask:()=>aiView('ask'),story:()=>aiView('story'),library:libraryView,handoff:handoffView,reminders:remindersView,settings:settingsView,export:exportView,sync:syncView,profile:profileView,player:playerView};
  $('#screen').innerHTML=(views[p]||homeView)();
  updatePlayer();icons();
}
function totals(){const rows=state.empty?[]:state.events;return {ml:rows.filter(e=>e.kind==='feed').reduce((n,e)=>n+Number((e.title.match(/[0-9]+/)||[0])[0]),0),feeds:rows.filter(e=>e.kind==='feed').length,diapers:rows.filter(e=>e.kind==='diaper').length,sleep:rows.filter(e=>e.kind==='sleep').reduce((n,e)=>n+(e.minutes??Number((e.title.match(/[0-9]+/)||[0])[0])),0)};}
function homeView(){
  $('#app-header').innerHTML=header('晚上好，慢慢来',state.empty?'10 月 2 日 · 周五 · 小西瓜 · 生日未填写':'10 月 2 日 · 周五 · 小西瓜 6 个月','profile');
  return `${syncText()}${state.empty?notice('欢迎，小西瓜。宝宝资料已准备好，记录从今天开始。'):''}
  ${state.sleep?`<div class="hero"><div class="kicker">${icon('moon')} 宝宝正在睡觉</div><h2>留一点安静，<br>也留一点时间给你。</h2><div class="hero-time"><b>${String(Math.floor(state.sleepMinutes/60)).padStart(2,'0')}:${String(state.sleepMinutes%60).padStart(2,'0')}</b><span>已睡时长</span></div><div class="hero-foot">今天 ${state.sleepStart} 入睡 · 计时持续中</div><button class="primary-button" data-action="end-sleep">${icon('sunrise')}宝宝醒了，结束睡眠</button></div>`:`<div class="hero"><div class="kicker">${icon('heart')} 小西瓜的照护日记</div><h2>${state.empty?'每一小步，<br>都值得轻轻记下。':'这一刻，<br>按你们的节奏来。'}</h2><p class="hero-foot">${state.empty?'暂无记录。离线也可以保存。':'宝宝醒着，准备睡觉时开始计时。'}</p><button class="primary-button" data-action="start-sleep">${icon('moon')}开始睡眠</button></div>`}
  ${section('快速记录')}<div class="quick-grid"><button class="quick" data-action="feed"><span class="quick-icon">${icon('milk')}</span><b>喂奶</b><small>奶量 / 时间</small></button><button class="quick" data-page="sleep"><span class="quick-icon">${icon('moon')}</span><b>睡眠</b><small>${state.sleep?'正在计时':'开始 / 补记'}</small></button><button class="quick" data-action="diaper"><span class="quick-icon">${icon('baby')}</span><b>尿布</b><small>尿尿 / 便便</small></button></div>
  <button class="voice-strip" data-action="voice-record"><span class="icon">${icon('mic')}</span><span><b>说一句，就能记</b><small>“刚喝了 150 毫升配方奶”</small></span>${icon('chevron-right')}</button>
  ${section('今天的小结','查看记录','history')}<div class="stats-grid"><div class="stat"><span>喂奶</span><b>${totals().ml}<small>ml</small></b><div class="secondary">${totals().feeds} 次 · 瓶喂总量</div></div><div class="stat"><span>睡眠</span><b>${(totals().sleep/60).toFixed(1)}<small>小时</small></b><div class="secondary">完成的睡眠</div></div><div class="stat"><span>尿布</span><b>${totals().diapers}<small>次</small></b><div class="secondary">${state.empty?'暂无记录':'尿 4 · 便 1'}</div></div></div>
  <p class="hint">${state.empty?'保存第一条记录后，这里会出现今日摘要。':`本机示例汇总 · 含 ${state.pending} 项待同步。正在进行的睡眠单独显示。`}</p>
  <div class="handoff-preview"><button class="row" data-page="handoff"><span class="row-icon">${icon('heart-handshake')}</span><span class="row-content"><b>照护交接</b><small>${state.empty?'暂无交接留言':'把重要的小事，交给下一位照护者'}</small></span>${icon('chevron-right')}</button></div>`;
}
function stats(){return `<div class="stats-grid"><div class="stat"><span>瓶喂奶量</span><b>${state.range==='今天'?totals().ml:state.empty?'0':'3,510'}<small>ml</small></b><div class="secondary">${state.range==='今天'?totals().feeds:state.empty?'0':'24'} 次喂奶</div></div><div class="stat"><span>已完成睡眠</span><b>${state.range==='今天'?(totals().sleep/60).toFixed(1):state.empty?'0':'72.8'}<small>小时</small></b><div class="secondary">${state.range==='今天'?'进行中另计':'跨午夜按日分配'}</div></div><div class="stat"><span>尿布</span><b>${state.range==='今天'?totals().diapers:state.empty?'0':'36'}<small>次</small></b><div class="secondary">${state.empty?'暂无记录':'尿尿与便便分别统计'}</div></div></div>`;}
function historyView(){
  $('#app-header').innerHTML=header('记录与统计','每一小步，都有迹可循','add');
  const items=state.empty?[]:state.events.filter(e=>state.filter==='全部'||(state.filter==='喂奶'&&e.kind==='feed')||(state.filter==='睡眠'&&e.kind==='sleep')||(state.filter==='尿布'&&e.kind==='diaper'));
  return `<div class="segment">${['今天','近 7 天','本月'].map(r=>`<button data-action="range" data-value="${r}" class="${r===state.range?'active':''}">${r}</button>`).join('')}</div><div class="date-control"><button data-action="previous-day" aria-label="前一天">${icon('chevron-left')}</button><input type="date" value="2026-10-03" aria-label="记录日期"><button data-action="next-day" aria-label="后一天">${icon('chevron-right')}</button></div>${stats()}
  ${state.range!=='今天'?`<div class="chart" aria-label="示例七天睡眠时长柱图">${[75,65,81,59,84,78,72].map((h,i)=>`<div class="chart-col ${i===6?'today':''}"><div class="chart-bar" style="height:${state.empty?2:h}px"></div><small>${26+i>30?26+i-30:26+i}</small></div>`).join('')}</div><div class="chart-legend"><span>9/26 – 10/2 · 睡眠小时数</span><span>示例图</span></div>`:''}
  ${notice(state.pending?`${state.pending} 项待同步。本机统计包含离线新增，云端确认统计将在连接后更新。`:'本机示例统计。连接服务后显示统计修订与更新时间。',state.pending?'warning':'')}
  <div class="chips">${['全部','喂奶','睡眠','尿布'].map(f=>`<button class="chip ${state.filter===f?'active':''}" data-action="filter" data-value="${f}">${f}</button>`).join('')}</div>
  ${state.sleep&&state.filter!=='尿布'&&state.filter!=='喂奶'?`<button class="row" data-page="sleep"><span class="row-icon lavender">${icon('moon')}</span><span class="row-content"><b>睡眠进行中 · 42 分钟</b><small>今天 20:58 开始</small></span><span class="pill">进行中</span></button>`:''}
  <div class="timeline">${items.length?items.map(e=>eventRow(e)).join(''):empty('还没有这类记录','保存后会在这里按时间排列。补记可以选择过去的日期和时间。','记录一件小事')}</div>${section('更多记录')}<div class="group"><button class="row" data-action="extra" data-value="洗澡"><span class="row-icon">${icon('bath')}</span><span class="row-content"><b>洗澡</b><small>时间与备注</small></span>${icon('plus')}</button><button class="row" data-action="extra" data-value="趴趴练习"><span class="row-icon peach">${icon('activity')}</span><span class="row-content"><b>趴趴练习</b><small>时长与备注</small></span>${icon('plus')}</button></div><button class="text-button" data-page="export">${icon('download')}导出这个时间范围</button>`;
}
function eventRow(e){return `<button class="row" data-action="event-detail" data-id="${e.id}"><time>${e.time}</time><span class="row-icon ${e.kind==='feed'?'peach':e.kind==='sleep'?'lavender':''}">${icon(e.kind==='feed'?'milk':e.kind==='sleep'?'moon':'baby')}</span><span class="row-content"><b>${esc(e.title)}</b><small>${esc(e.note)}</small></span>${e.pending?'<span class="pill pending">待同步</span>':icon('chevron-right')}</button>`;}
function sleepView(){
  $('#app-header').innerHTML=header('睡眠','睡着的时光，也轻轻记下','none',true);
  return `<div class="hero sleep-hero"><div class="kicker">${icon(state.sleep?'moon':'sun')} ${state.sleep?'宝宝正在睡觉':'宝宝醒着'}</div><div class="hero-time"><b>${state.sleep?String(Math.floor(state.sleepMinutes/60)).padStart(2,'0')+':'+String(state.sleepMinutes%60).padStart(2,'0'):'00:00'}</b></div><div class="hero-foot">${state.sleep?`今天 ${state.sleepStart} 开始 · 退出页面继续计时`:'准备好时再开始，不需要一直开着屏幕'}</div><div class="sleep-actions"><button class="primary-button" data-action="${state.sleep?'end-sleep':'start-sleep'}">${icon(state.sleep?'sunrise':'moon')}${state.sleep?'宝宝醒了':'开始睡眠'}</button></div></div>${notice('计时状态会持久保存。手机锁屏、切页或进程回收后可恢复；当前原型展示的是状态示例。')}
  <button class="secondary-button" data-action="sleep-backfill">${icon('calendar-plus')}补记一段睡眠</button>${section('今天的睡眠')}${state.empty?empty('还没有睡眠记录','开始计时，或补记已经结束的睡眠。'): `<div class="group"><div class="row"><span class="row-icon lavender">${icon('moon')}</span><span class="row-content"><b>夜间睡眠</b><small>昨天 21:15 – 今天 06:50</small></span><span class="row-meta"><b>9 小时 35 分</b><small>按当天时段计入</small></span></div><div class="row"><span class="row-icon lavender">${icon('moon')}</span><span class="row-content"><b>下午小睡</b><small>15:08 – 16:20</small></span><span class="row-meta"><b>1 小时 12 分</b></span></div></div>`}${section('睡前陪伴','打开音频库','library')}<button class="row" data-action="play" data-value="轻柔雨声"><span class="row-icon">${icon('cloud-rain')}</span><span class="row-content"><b>轻柔雨声</b><small>白噪音 · 可设置定时停止</small></span>${icon('play')}</button>`;
}
function companionView(){
  $('#app-header').innerHTML=header('陪伴','照护你，也陪着小西瓜','none');
  return `<h2 class="prompt-title">需要答案，<br>还是一段好故事？</h2><p class="hint">一句话提问。安静时听故事。<br>把双手留给宝宝。</p>
  <button class="feature" data-page="ask"><div class="feature-head"><span class="feature-icon">${icon('message-circle')}</span><div><h2>语音问答</h2><p>问照护问题，也能整理记录</p></div></div><div class="feature-link">开始聊聊${icon('arrow-right')}</div></button>
  <button class="feature peach" data-page="story"><div class="feature-head"><span class="feature-icon">${icon('book-open')}</span><div><h2>讲故事</h2><p>为今晚，准备一段温柔的冒险</p></div></div><div class="feature-link">选一个故事主题${icon('arrow-right')}</div></button>
  <button class="feature lavender" data-page="library"><div class="feature-head"><span class="feature-icon">${icon('headphones')}</span><div><h2>音频库</h2><p>白噪音 · 儿歌 · 故事 · 轻音乐</p></div></div><div class="feature-link">找到适合此刻的声音${icon('arrow-right')}</div></button>`;
}
function aiView(kind){
  const isAsk=kind==='ask',job=state[kind];
  $('#app-header').innerHTML=header(isAsk?'语音问答':'讲故事',isAsk?'把想问的，说给我听':'一段故事，一点温柔的陪伴','none',true);
  if(job.status==='processing')return `${notice('请求保留在自己的页面。切到其他功能，结果仍只回到这里。')}<div class="question-bubble">${esc(job.prompt)}</div><div class="request-status"><div class="loader"></div><b>${isAsk?'正在整理答案':'正在写一段故事'}</b><p>可以离开这页，稍后回来查看。</p><button class="text-button" data-action="cancel-job" data-kind="${kind}">取消这次请求</button></div>`;
  if(job.status==='failed')return `<div class="question-bubble">${esc(job.prompt)}</div>${notice(state.offline?'暂时没有网络，问题已保留。连接后由你决定是否发送。':'这次请求没有完成。原问题已保留，可以重试或修改后再发送。','error','circle-alert')}<button class="primary-button" data-action="retry-job" data-kind="${kind}">${icon('rotate-cw')}重试这次请求</button><button class="text-button" data-action="edit-job" data-kind="${kind}">修改文字</button>`;
  if(job.status==='complete')return `<div class="question-bubble">${esc(job.prompt)}</div><div class="answer-label">${icon(isAsk?'sparkles':'book-open')} ${isAsk?'照护回答':'今晚的故事'} · 示例内容</div><div class="answer">${isAsk?`<p>可以先留意小西瓜的精神、吃奶情况和尿布记录，把变化记下来。</p><p>把担心的时间、频率和表现说具体一点，更容易整理下一步要关注的事。你也可以把今天的记录带给儿科医生看。</p><p>如果出现呼吸困难、异常嗜睡等紧急情况，及时联系当地医疗服务。</p>`:`<h3>小熊和一盏月亮灯</h3><p>森林安静下来的时候，小熊发现窗边有一小束月光。它不着急赶路，只轻轻推开门，去看看今天的夜晚。</p><p>萤火虫点亮了一条短短的小路。小熊走得很慢，听见树叶沙沙地说：“你今天已经做得很好了。”</p><p>小熊把这句话带回家，放在柔软的枕头旁。月亮灯不需要守一整夜，困了就闭上眼睛。明天醒来，世界依然温柔。晚安，小熊。晚安，小西瓜。</p>`}</div><button class="secondary-button" data-action="read-reply" data-kind="${kind}">${icon('volume-2')}${isAsk?'朗读这段回答':'朗读这个故事'}</button><div class="section-heading"><button class="text-button" data-action="new-job" data-kind="${kind}">${icon('plus')}${isAsk?'继续提问':'再讲一个'}</button><button class="text-button" data-action="save-story" data-kind="${kind}">${icon('bookmark')}保存${isAsk?'回答':'故事'}</button></div><p class="hint">${isAsk?'AI 回答作为信息参考，不替代专业诊疗。':'当前为示例故事。文字保留，音频失败时可单独重试。'}</p>`;
  return `<h2 class="prompt-title">${isAsk?'照护的疑问，<br>从一句话开始。':'今晚想听，<br>怎样的故事？'}</h2><p class="hint">${isAsk?'可以打字，也可以点击麦克风说话。<br>录音停止后先确认文字，再发送。':'选择主题，或说出你想听的内容。<br>故事与问答使用独立会话。'}</p>
  <div class="suggestions">${(isAsk?['帮我整理今天的照护记录','睡前如何安静下来','生成照护交接摘要']:['小熊与月亮','森林里的晚安','关于分享的小故事']).map(text=>`<button class="suggestion" data-action="suggest" data-kind="${kind}" data-value="${text}">${text}</button>`).join('')}</div>
  ${!isAsk?'<div class="story-preferences"><div class="preference"><label for="story-duration">时长</label><select id="story-duration"><option>约 3 分钟</option><option>约 5 分钟</option></select></div><div class="preference"><label for="story-tone">语气</label><select id="story-tone"><option>温柔晚安</option><option>轻快探索</option></select></div></div>':''}
  <div class="composer"><textarea id="${isAsk?'question-input':'story-input'}" placeholder="${isAsk?'写下你的问题…':'说说故事的主角或主题…'}" aria-label="${isAsk?'问题文字':'故事主题'}">${esc(job.draft)}</textarea><div class="composer-foot"><button class="text-button" data-action="voice-ai" data-kind="${kind}">${icon('mic')}语音输入</button><button class="primary-button" data-action="send-job" data-kind="${kind}">${isAsk?'发送问题':'生成故事'}${icon('arrow-up')}</button></div></div>
  <p class="hint">${isAsk?'需要引用记录时，发送前会显示将使用的范围。不会自动保存未确认的语音记录。':'开始朗读由你决定。手机音频焦点会协调录音、来电与其他播放。'}</p>`;
}
const tracks=[{name:'轻柔雨声',cat:'白噪音',detail:'自然声音 · 30 分钟',i:'cloud-rain',color:''},{name:'森林里的晚安',cat:'故事',detail:'温柔晚安 · 4 分钟',i:'book-open',color:'peach'},{name:'小星星',cat:'儿歌',detail:'轻声儿歌 · 2 分钟',i:'music-2',color:'peach'},{name:'安静的钢琴',cat:'轻音乐',detail:'纯音乐 · 12 分钟',i:'piano',color:'lavender'},{name:'轻柔海浪',cat:'白噪音',detail:'自然声音 · 20 分钟',i:'waves',color:''},{name:'摇篮曲',cat:'轻音乐',detail:'纯音乐 · 8 分钟',i:'music',color:'lavender'}];
function libraryView(){
  $('#app-header').innerHTML=header('音频库','找一段，适合此刻的声音','none',true);
  return `<div class="search">${icon('search')}<input id="library-search" placeholder="搜索声音、故事或儿歌" aria-label="搜索音频"></div><div class="chips">${['全部','白噪音','儿歌','故事','轻音乐','已下载'].map(c=>`<button class="chip ${c===state.category?'active':''}" data-action="category" data-value="${c}">${c}</button>`).join('')}</div>${notice('示例目录，尚未连接服务。实际分类与可用音质以服务能力为准。')}
  <div class="group" id="track-list">${trackRows('')}</div><p class="hint">下载和播放缓存有容量上限。已下载内容可离线播放；清理缓存会保留记录与收藏。</p>`;
}
function trackRows(search){return tracks.filter(t=>(state.category==='全部'||state.category===t.cat||(state.category==='已下载'&&state.downloaded.has(t.name)))&&t.name.includes(search)).map(t=>`<div class="row library-row"><button class="album ${t.color}" data-action="play" data-value="${t.name}" aria-label="播放 ${t.name}">${icon(t.i)}</button><div class="row-content"><b>${t.name}</b><small>${t.detail}</small>${state.downloaded.has(t.name)?'<span class="download-status">已下载 · 可离线</span>':''}</div><button class="icon-button" data-action="download-track" data-value="${t.name}" aria-label="下载 ${t.name}">${icon(state.downloaded.has(t.name)?'circle-check':'download')}</button><button class="icon-button" data-action="play" data-value="${t.name}" aria-label="播放 ${t.name}">${icon('play')}</button></div>`).join('')||empty('这里还没有音频',state.category==='已下载'?'选择一个声音，点击下载。':'换一个分类或搜索词。','',null,'headphones');}
function filterTracks(q){$('#track-list').innerHTML=trackRows(q);icons();}
function handoffView(){
  $('#app-header').innerHTML=header('照护交接','重要的小事，不用凭记忆','none',true);
  return `${notice(state.empty?'暂无记录。第一次交接可以先写下照护习惯。':'本机最新记录 · 2 项待同步。示例信息尚未获得云端确认。',state.empty?'':'warning')}${section('现在的状态')}<div class="group"><div class="row"><span class="row-icon peach">${icon('milk')}</span><span class="row-content"><b>最近喂奶</b><small>${state.empty?'暂无记录':'19:40 · 配方奶 150 ml'}</small></span></div><div class="row"><span class="row-icon lavender">${icon('moon')}</span><span class="row-content"><b>睡眠</b><small>${state.sleep?'20:58 开始 · 正在睡觉':state.empty?'暂无记录':'当前没有计时中的睡眠'}</small></span></div><div class="row"><span class="row-icon">${icon('baby')}</span><span class="row-content"><b>最近尿布</b><small>${state.empty?'暂无记录':'18:55 · 尿尿 · 待同步'}</small></span></div></div>${section('留给照护者的话')}<div class="note-editor"><label for="handoff-note">今天需要留意</label><textarea id="handoff-note" maxlength="500" placeholder="比如奶的准备、入睡习惯、要带的物品…">${state.empty?'':esc(state.handoffNote)}</textarea><small>会保存到本机，再排队同步</small></div><button class="primary-button" data-action="save-handoff">${icon('check')}保存交接留言</button><div class="section-heading"><button class="text-button" data-action="speak-handoff">${icon('volume-2')}朗读交接</button><button class="text-button" data-action="share-handoff">${icon('share-2')}分享文字</button></div><p class="hint">分享前会打开系统预览，由你选择接收者。没有记录不代表没有发生。</p>`;
}
function remindersView(){
  $('#app-header').innerHTML=header('提醒','轻轻提醒，一件件完成','none',true);
  return `${notice('通知权限：未启用。提醒会保留在列表；启用后由系统发送通知。','warning','bell-off')}<button class="secondary-button" data-action="notification-permission">${icon('bell')}管理通知权限</button>${section('接下来的提醒')}<div class="group">${state.reminders.length?state.reminders.map((r,i)=>`<button class="row" data-action="manage-reminder" data-id="${i}"><span class="row-icon">${icon(r.done?'circle-check':'bell')}</span><span class="row-content"><b>${esc(r.title)}</b><small>今天 ${r.time}${r.done?' · 已完成':''}</small></span>${icon('chevron-right')}</button>`).join(''):empty('还没有提醒','为需要记住的小事设置时间。')}</div><button class="primary-button" data-action="add-reminder">${icon('plus')}新建提醒</button><p class="hint">提醒只通知，不自动执行照护操作。系统节电与权限可能影响通知时间。</p>`;
}
function settingsView(){
  $('#app-header').innerHTML=header('我的','一个家的照护空间','none');
  return `<button class="settings-profile" data-page="profile" style="width:100%;text-align:left"><span class="profile-button">瓜</span><span><b>小西瓜</b><small>${state.empty?'生日未填写':'6 个月'} · 默认宝宝档案</small></span>${icon('chevron-right')}</button>${section('连接与数据')}<div class="group"><button class="row" data-page="sync"><span class="row-icon">${icon('cloud')}</span><span class="row-content"><b>同步状态</b><small>${state.pending?`${state.pending} 项待同步`:'没有待同步操作'} · ${state.offline?'当前离线':'未连接服务'}</small></span>${icon('chevron-right')}</button><button class="row" data-action="connection-settings"><span class="row-icon">${icon('link')}</span><span class="row-content"><b>家庭服务连接</b><small>填写后端地址与访问凭据</small></span>${icon('chevron-right')}</button><button class="row" data-page="export"><span class="row-icon">${icon('download')}</span><span class="row-content"><b>导出照护记录</b><small>CSV / JSON · 选择时间范围</small></span>${icon('chevron-right')}</button></div>${section('使用偏好')}<div class="group"><button class="row" data-action="theme"><span class="row-icon lavender">${icon('moon')}</span><span class="row-content"><b>夜间模式</b><small>柔和对比，减少深夜刺激</small></span><span class="toggle ${state.night?'on':''}"></span></button><button class="row" data-action="large"><span class="row-icon">${icon('type')}</span><span class="row-content"><b>大字阅读</b><small>同时尊重系统字体设置</small></span><span class="toggle ${state.large?'on':''}"></span></button><button class="row" data-page="reminders"><span class="row-icon peach">${icon('bell')}</span><span class="row-content"><b>提醒与通知</b><small>权限、计划与完成状态</small></span>${icon('chevron-right')}</button><button class="row" data-action="permissions"><span class="row-icon">${icon('mic')}</span><span class="row-content"><b>麦克风权限</b><small>${state.permission?'已拒绝 · 仍可打字':'使用语音时再请求'}</small></span>${icon('chevron-right')}</button></div>${section('声音与存储')}<div class="group"><button class="row" data-action="cache-settings"><span class="row-icon lavender">${icon('hard-drive')}</span><span class="row-content"><b>下载与缓存</b><small>示例使用 12 MB / 上限 256 MB</small></span>${icon('chevron-right')}</button><button class="row" data-action="device-settings"><span class="row-icon">${icon('smartphone')}</span><span class="row-content"><b>配套设备</b><small>需要时再连接硬件</small></span>${icon('chevron-right')}</button></div><p class="hint">西瓜育儿 · Android 设计原型<br>本机演示，无线上连接。个人自用，无注册。</p>`;
}
function exportView(){
  $('#app-header').innerHTML=header('导出记录','带走完整、可读的照护历史','none',true);
  return `<div class="form-group"><label>时间范围</label><div class="field-row"><input type="date" value="2026-09-26" aria-label="导出开始日期"><input type="date" value="2026-10-03" aria-label="导出结束日期"></div><p class="hint">Asia/Shanghai · 按当地日期包含结束日</p></div>${section('文件格式')}<div class="export-options">${[['CSV','file-spreadsheet','便于表格查看'],['JSON','file-json','保留字段、修订与同步标记']].map(([f,i,sub])=>`<button class="export-option ${state.exportFormat===f?'active':''}" data-action="export-format" data-value="${f}">${icon(i)}<div><b>${f}</b><small>${sub}</small></div><span class="radio-dot"></span></button>`).join('')}</div><div class="form-group"><label for="export-scope">记录范围</label><select id="export-scope"><option>本机全部记录（含待同步标记）</option><option>仅已同步记录</option></select></div>${notice('包含喂奶、睡眠、尿布、备注和发生时间；不包含访问凭据。导出前会显示条数与时间范围。')}<button class="primary-button" data-action="export-preview">${icon('download')}生成导出预览</button>`;
}
function syncView(){
  $('#app-header').innerHTML=header('同步状态','每条修改都有清楚的去向','none',true);
  return `<div class="hero"><div class="kicker">${icon(state.offline?'cloud-off':'cloud')} ${state.offline?'当前离线':'尚未连接家庭服务'}</div><h2>${state.pending?`${state.pending} 项修改，<br>已安全保存在本机。`:'记录会先保存在本机。'}</h2><p class="hero-foot">待同步不会阻止下一条记录</p><button class="primary-button" data-action="sync-now">${icon('refresh-cw')}立即同步</button></div>${notice('此原型不发送数据。真实应用会在收到服务端确认后减少待同步数量。')}${section('待同步操作')}<div class="group">${state.pending?`<div class="row"><span class="row-icon">${icon('baby')}</span><span class="row-content"><b>新增尿布记录</b><small>今天 18:55 · 等待连接</small></span><span class="pill pending">待发送</span></div><div class="row"><span class="row-icon peach">${icon('milk')}</span><span class="row-content"><b>新增喂奶记录</b><small>今天 15:00 · 等待连接</small></span><span class="pill pending">待发送</span></div>`:empty('暂无待同步操作','新增、修改和删除都会使用持久操作队列。')}</div><button class="text-button" data-action="simulate-conflict">查看冲突处理示例</button><p class="hint">统计携带档案、修订、时区与时间范围。服务端更新不会与本机新增重复累加。</p>`;
}
function profileView(){
  $('#app-header').innerHTML=header('宝宝资料','默认档案已经准备好','none',true);
  return `<div class="form-group"><label for="baby-name">昵称</label><input id="baby-name" value="小西瓜" maxlength="32"></div><div class="form-group"><label for="baby-birthday">生日（可稍后填写）</label><input id="baby-birthday" type="date" value="${state.empty?'':'2026-04-02'}"><p class="hint">这是示例生日。首次启动保留为空，不猜测真实年龄。</p></div><div class="form-group"><label for="timezone">统计时区</label><select id="timezone"><option>Asia/Shanghai (UTC+08:00)</option><option>跟随手机所在时区</option></select><p class="hint">更改时区会改变每天的统计边界，已有事件的原始时间仍保留。</p></div><button class="primary-button" data-action="save-profile">${icon('check')}保存资料</button><p class="hint">个人家庭自用，一名宝宝，无需账号注册。成人应用的数据与宝宝档案保持分离。</p>`;
}
function playerView(){
  $('#app-header').innerHTML=header('正在播放','来自音频库 / 故事朗读','none',true);
  if(!state.player)return empty('还没有选择声音','去音频库找到适合现在的内容。','打开音频库','open-library','headphones');
  return `<div class="player-art">${icon(state.player.kind==='story'?'book-open':'headphones')}</div><h2 class="player-title">${esc(state.player.title)}</h2><p class="player-subtitle">${state.playback?'正在播放':'已暂停'} · 状态演示</p><input class="play-slider" type="range" min="0" max="100" value="34" aria-label="播放进度"><div class="play-times"><span>01:24</span><span>04:10</span></div><div class="player-controls"><button class="icon-button" data-action="seek" data-value="-15" aria-label="后退十五秒">${icon('rotate-ccw')}</button><button class="icon-button main" data-action="toggle-play" aria-label="${state.playback?'暂停':'继续播放'}">${icon(state.playback?'pause':'play')}</button><button class="icon-button" data-action="seek" data-value="15" aria-label="前进十五秒">${icon('rotate-cw')}</button></div><div class="player-extra"><button data-action="sleep-timer">${icon('timer')}定时停止</button><button data-action="stop-play">${icon('square')}停止播放</button><button data-page="library">${icon('list-music')}切换音频</button></div>${notice('锁屏可从系统媒体通知控制。来电或录音会协调音频焦点；后台播放遵守 Android 系统规则。')}`;
}
function updatePlayer(){
  if(!state.player||state.page==='player'){$('#mini-player').innerHTML='';return;}
  $('#mini-player').innerHTML=`<div class="mini-player"><button class="album lavender" data-page="player" aria-label="打开播放器">${icon(state.player.kind==='story'?'book-open':'headphones')}</button><button class="mini-text" data-page="player"><b>${esc(state.player.title)}</b><small>${state.playback?'正在播放':'已暂停'} · 01:24 / 04:10</small></button><button class="icon-button play-toggle" data-action="toggle-play" aria-label="${state.playback?'暂停':'继续播放'}">${icon(state.playback?'pause':'play')}</button><button class="icon-button" data-action="stop-play" aria-label="停止播放">${icon('x')}</button></div><div class="mini-progress"><span></span></div>`;
}
function sheet(title,body,subtitle=''){
  $('#overlay').innerHTML=`<div class="overlay-backdrop"><section class="sheet" role="dialog" aria-modal="true" aria-label="${title}"><div class="sheet-grip"></div><div class="sheet-heading"><h2>${title}</h2><button class="icon-button" data-action="close-sheet" aria-label="关闭">${icon('x')}</button></div>${subtitle?`<p class="sheet-subtitle">${subtitle}</p>`:''}${body}</section></div>`;icons();
}
function closeSheet(){state.sheet=null;$('#overlay').innerHTML='';}
function feedSheet(edit=false){
  state.sheet='feed';sheet(edit?'编辑喂奶记录':'记录喂奶',`<div class="amount-control"><button data-action="amount" data-value="-10" aria-label="减少十毫升">${icon('minus')}</button><input id="feed-amount" type="number" inputmode="numeric" min="1" max="2000" value="${state.feedAmount}" aria-label="奶量毫升"><button data-action="amount" data-value="10" aria-label="增加十毫升">${icon('plus')}</button></div><div class="amount-unit">毫升 · ml</div><div class="amount-chips">${[90,120,150,180].map(n=>`<button data-action="set-amount" data-value="${n}">${n} ml</button>`).join('')}</div><div class="form-group"><label>喂养方式</label><div class="choice-grid">${['配方奶','母乳瓶喂','亲喂'].map(k=>`<button class="choice ${k===state.milkKind?'active':''}" data-action="milk-kind" data-value="${k}">${k}</button>`).join('')}</div><p class="hint">亲喂记录时长与侧别，不强行填写奶量。</p></div><div class="form-group"><label for="record-time">发生时间</label><input id="record-time" type="datetime-local" value="2026-10-02T21:40"></div><div class="form-group"><label for="record-note">备注（可选）</label><input id="record-note" placeholder="比如吃奶情况…"></div><button class="primary-button" data-action="save-feed" data-edit="${edit}">${icon('check')}保存喂奶记录</button>`,'先保存在手机。离线时自动排队同步。');
}
function diaperSheet(){
  state.sheet='diaper';sheet('记录尿布',`<div class="form-group"><label>这次是什么？</label><div class="choice-grid">${['尿尿','便便','都有'].map(k=>`<button class="choice ${k===state.diaperKind?'active':''}" data-action="diaper-kind" data-value="${k}">${k}</button>`).join('')}</div></div><div class="form-group"><label for="record-time">发生时间</label><input id="record-time" type="datetime-local" value="2026-10-02T21:40"></div><div class="form-group"><label for="record-note">备注（可选）</label><textarea id="record-note" placeholder="颜色、性状，或想记住的小事…"></textarea></div><button class="primary-button" data-action="save-diaper">${icon('check')}保存尿布记录</button>`,'点选、保存。备注可以随时补充。');
}
function voiceSheet(mode){
  state.voiceMode=mode;state.voiceState='idle';state.sheet='voice';renderVoice();
}
function renderVoice(){
  const stage=state.voiceState, mode=state.voiceMode;
  let body='';
  if(stage==='permission')body=`<div class="voice-sheet"><div class="voice-orb">${icon('mic-off')}</div><h3>麦克风权限未开启</h3><p>录音需要麦克风权限。<br>你仍然可以用系统输入法打字。</p><button class="primary-button" data-action="open-system-settings">打开系统设置</button><button class="text-button" data-action="voice-type" style="margin:8px auto">改用文字输入</button></div>`;
  else if(stage==='recording')body=`<div class="voice-sheet"><div class="voice-orb recording">${icon('mic')}</div><h3>正在听你说</h3><p>${mode==='record'?'说出事件、时间和数量。':'说完后停止，先确认文字。'}</p><div class="waveform" aria-label="录音状态示意">${[14,24,31,18,36,25,16,30,20].map((h,i)=>`<span style="height:${h}px;animation-delay:${i/9}s"></span>`).join('')}</div><div class="recording-time">00:07</div><p>由你停止录音 · 原型没有实际录音</p><button class="primary-button" data-action="stop-voice">${icon('square')}停止并识别</button><button class="text-button" data-action="cancel-voice" style="margin:8px auto">取消，丢弃录音</button></div>`;
  else if(stage==='review')body=`<div class="voice-sheet"><p class="transcript-label">识别文字 · 可以点击修改</p><textarea class="transcript" id="voice-transcript" aria-label="识别文字">${mode==='record'?'小西瓜刚喝了150毫升配方奶':mode==='story'?'讲一个小熊与月亮的晚安故事':'帮我整理今天的照护记录'}</textarea><p>还没有发送，也没有保存记录。</p><div class="sheet-footer"><button class="secondary-button" data-action="start-voice">重新录音</button><button class="primary-button" data-action="confirm-voice">${mode==='record'?'预览记录':'填入文字'}${icon('check')}</button></div><button class="text-button" data-action="cancel-voice" style="margin:8px auto">取消</button></div>`;
  else body=`<div class="voice-sheet"><div class="voice-orb">${icon('mic')}</div><h3>${mode==='record'?'说一句，记一件事':'准备好，就开始说'}</h3><p>${mode==='record'?'“刚喝了 150 毫升配方奶”<br>“下午三点换了尿布”':'点击开始录音，点击停止后识别。<br>发送前，你可以检查并修改文字。'}</p><button class="primary-button" data-action="start-voice">${icon('mic')}开始录音</button><button class="text-button" data-action="voice-type" style="margin:8px auto">改用文字输入</button><p style="font-size:11px">离开应用或来电时会停止录音，并保留已有文字草稿。</p></div>`;
  sheet('语音输入',body);icons();
}
function previewRecord(text){sheet('确认这条记录',`<div class="question-bubble">${esc(text)}</div><div class="group"><div class="row"><span class="row-icon peach">${icon('milk')}</span><span class="row-content"><b>喂奶 · 150 ml</b><small>配方奶 · 今天 21:40</small></span></div></div><p class="hint">从语音整理出的记录只在确认后保存。</p><div class="sheet-footer"><button class="secondary-button" data-action="feed">修改</button><button class="primary-button" data-action="save-feed">确认保存</button></div>`);}
function saveEvent(kind,title,note){const id=Date.now();state.events.unshift({id,kind,title,note,time:'21:40',pending:true});state.empty=false;state.pending++;state.lastUndo=id;closeSheet();render();toast('已保存到手机 · 等待同步','撤销','undo');}
function toast(message,label='',action=''){clearTimeout(state.toastTimer);$('#toast').innerHTML=`<span>${esc(message)}</span>${label?`<button data-action="${action}">${label}</button>`:''}`;$('#toast').classList.add('show');state.toastTimer=setTimeout(()=>$('#toast').classList.remove('show'),label?7000:3500);}
function sendJob(kind){const job=state[kind],draft=kind==='ask'?$('#question-input'):$('#story-input');if(draft)job.draft=draft.value;if(!job.draft.trim()){toast('先写下问题或主题，也可以语音输入');return;}job.prompt=job.draft.trim();job.status='processing';job.requestId++;const id=job.requestId,fail=state.failNext||state.offline;state.failNext=false;$('#failure-demo').checked=false;render();setTimeout(()=>{if(job.requestId!==id)return;job.status=fail?'failed':'complete';job.result=job.prompt;if(state.page===kind)render();else toast(kind==='ask'?'问答结果已准备好':'故事已准备好','查看',kind==='ask'?'open-ask':'open-story');},1600);}
function legacyClick(e){
  const button=e.target.closest('button');if(!button)return;if(button.dataset.page){go(button.dataset.page);return;}
  const a=button.dataset.action,v=button.dataset.value,kind=button.dataset.kind;
  if(a==='back'){go(['ask','story','library','player'].includes(state.page)?'companion':state.page==='profile'||state.page==='sync'?'settings':'home');return;}
  if(a==='theme'){state.night=!state.night;render();return;}
  if(a==='large'){state.large=!state.large;$('#large-demo').checked=state.large;render();return;}
  if(a==='reset'){location.reload();return;}
  if(a==='feed'){state.lastSelected=null;feedSheet();return;}
  if(a==='diaper'){diaperSheet();return;}
  if(a==='close-sheet'){closeSheet();return;}
  if(a==='record-menu'){sheet('记录一件小事',`<div class="group"><button class="row" data-action="feed"><span class="row-icon peach">${icon('milk')}</span><span class="row-content"><b>喂奶</b><small>奶量、方式和时间</small></span>${icon('chevron-right')}</button><button class="row" data-page="sleep"><span class="row-icon lavender">${icon('moon')}</span><span class="row-content"><b>睡眠</b><small>计时或补记</small></span>${icon('chevron-right')}</button><button class="row" data-action="diaper"><span class="row-icon">${icon('baby')}</span><span class="row-content"><b>尿布</b><small>尿尿、便便或都有</small></span>${icon('chevron-right')}</button><button class="row" data-action="voice-record"><span class="row-icon">${icon('mic')}</span><span class="row-content"><b>语音记录</b><small>先识别，再确认</small></span>${icon('chevron-right')}</button></div>`);return;}
  if(a==='amount'||a==='set-amount'){const input=$('#feed-amount');state.feedAmount=a==='amount'?Math.max(1,Number(input.value)+Number(v)):Number(v);input.value=state.feedAmount;return;}
  if(a==='milk-kind'){state.feedAmount=Number($('#feed-amount').value);state.milkKind=v;feedSheet();if(v==='亲喂'){toast('实际应用切换为时长与侧别输入');}return;}
  if(a==='diaper-kind'){state.diaperKind=v;document.querySelectorAll('.choice[data-action="diaper-kind"]').forEach(el=>el.classList.toggle('active',el.dataset.value===v));return;}
  if(a==='save-feed'){const amount=$('#feed-amount')?Number($('#feed-amount').value):150;if(!Number.isFinite(amount)||amount<1||amount>2000){toast('请检查奶量，记录不会被静默改小');return;}const note=$('#record-note')?$('#record-note').value:'';if(button.dataset.edit==='true'&&state.lastSelected){const selected=state.events.find(x=>x.id===state.lastSelected);if(selected){selected.title=`喂奶 · ${amount} ml`;selected.note=state.milkKind+(note?' · '+note:'');selected.pending=true;state.pending++;closeSheet();render();toast('已保存修改 · 等待同步');}return;}saveEvent('feed',`喂奶 · ${amount} ml`,state.milkKind+(note?' · '+note:''));return;}
  if(a==='save-diaper'){saveEvent('diaper',`换尿布 · ${state.diaperKind}`,$('#record-note').value||'无备注');return;}
  if(a==='undo'){state.events=state.events.filter(x=>x.id!==state.lastUndo);state.pending++;state.lastUndo=null;$('#toast').classList.remove('show');render();toast('已撤销 · 删除操作等待同步');return;}
  if(a==='start-sleep'){state.sleep=true;state.sleepMinutes=0;state.sleepStart='21:40';state.empty=false;state.pending++;render();toast('睡眠已开始 · 离开页面继续计时');return;}
  if(a==='end-sleep'){sheet('宝宝醒了吗？',`<div class="group"><div class="row"><span class="row-icon lavender">${icon('moon')}</span><span class="row-content"><b>${state.sleepMinutes} 分钟睡眠</b><small>今天 ${state.sleepStart} – 21:40</small></span></div></div><p class="hint">结束后保存为一条完整睡眠记录，可以再编辑时间。</p><div class="sheet-footer"><button class="secondary-button" data-action="close-sheet">继续计时</button><button class="primary-button" data-action="confirm-end-sleep">确认结束</button></div>`);return;}
  if(a==='confirm-end-sleep'){state.sleep=false;saveEvent('sleep',`睡眠 · ${state.sleepMinutes} 分钟`,`${state.sleepStart} – 21:40`);return;}
  if(a==='sleep-backfill'){sheet('补记睡眠',`<div class="form-group"><label for="sleep-start">开始时间</label><input id="sleep-start" type="datetime-local" value="2026-10-02T15:08"></div><div class="form-group"><label for="sleep-end">结束时间</label><input id="sleep-end" type="datetime-local" value="2026-10-02T16:20"></div><div class="form-group"><label for="record-note">备注（可选）</label><input id="record-note" placeholder="比如午睡…"></div><p class="hint">跨午夜的睡眠保存为一条记录，统计按当天覆盖的时段分配。</p><button class="primary-button" data-action="save-backfill">保存睡眠记录</button>`);return;}
  if(a==='save-backfill'){const start=new Date($('#sleep-start').value),end=new Date($('#sleep-end').value);if(end<=start){toast('结束时间需要晚于开始时间');return;}const min=Math.round((end-start)/60000);saveEvent('sleep',`睡眠 · ${min} 分钟`,$('#sleep-start').value.slice(11)+' – '+$('#sleep-end').value.slice(11));return;}
  if(a==='range'){state.range=v;render();return;}
  if(a==='filter'){state.filter=v;render();return;}
  if(a==='previous-day'||a==='next-day'){const input=$('.date-control input');const d=new Date(input.value+'T12:00:00');d.setDate(d.getDate()+(a==='previous-day'?-1:1));input.value=d.toISOString().slice(0,10);toast('日期控件演示 · 汇总为固定示例');return;}
  if(a==='voice-ai'){voiceSheet(kind);return;}
  if(a==='voice-record'){voiceSheet('record');return;}
  if(a==='start-voice'){state.voiceState=state.permission?'permission':'recording';if(state.playback){state.playback=false;updatePlayer();}renderVoice();return;}
  if(a==='stop-voice'){state.voiceState='review';renderVoice();return;}
  if(a==='cancel-voice'){closeSheet();toast('录音已取消，没有发送');return;}
  if(a==='confirm-voice'){const text=$('#voice-transcript').value;if(state.voiceMode==='record'){previewRecord(text);}else{state[state.voiceMode].draft=text;const mode=state.voiceMode;closeSheet();go(mode);toast('文字已填入 · 检查后再发送');}return;}
  if(a==='voice-type'){const mode=state.voiceMode;closeSheet();if(mode==='record')feedSheet();else go(mode);return;}
  if(a==='open-system-settings'){toast('实际应用打开系统权限设置，返回后重新检查权限');return;}
  if(a==='suggest'){state[kind].draft=v;render();return;}
  if(a==='send-job'){sendJob(kind);return;}
  if(a==='retry-job'){state[kind].draft=state[kind].prompt;sendJob(kind);return;}
  if(a==='cancel-job'){state[kind].requestId++;state[kind].status='idle';render();toast('本次请求已取消');return;}
  if(a==='edit-job'||a==='new-job'){state[kind].status='idle';if(a==='new-job')state[kind].draft='';render();return;}
  if(a==='open-ask'){go('ask');return;}
  if(a==='open-story'){go('story');return;}
  if(a==='read-reply'){startPlay(kind==='story'?'小熊和一盏月亮灯':'照护回答朗读',kind);return;}
  if(a==='save-story'){toast('已保存示例收藏 · 实际应用会持久保存文字');return;}
  if(a==='category'){state.category=v;render();return;}
  if(a==='download-track'){state.downloaded.add(v);render();toast('下载完成状态演示 · 未下载真实音频');return;}
  if(a==='play'){startPlay(v,'audio');return;}
  if(a==='toggle-play'){state.playback=!state.playback;if(state.page==='player')render();else{updatePlayer();icons();}return;}
  if(a==='stop-play'){state.playback=false;state.player=null;render();toast('已停止播放');return;}
  if(a==='open-library'){go('library');return;}
  if(a==='seek'){toast(v==='15'?'前进 15 秒 · 状态演示':'后退 15 秒 · 状态演示');return;}
  if(a==='sleep-timer'){sheet('定时停止播放',`<div class="choice-grid">${['15 分钟','30 分钟','60 分钟'].map(t=>`<button class="choice" data-action="set-sleep-timer" data-value="${t}">${t}</button>`).join('')}</div><button class="text-button" data-action="set-sleep-timer" data-value="关闭">关闭定时器</button>`);return;}
  if(a==='set-sleep-timer'){closeSheet();toast(v==='关闭'?'定时停止已关闭':`${v}后停止 · 状态演示`);return;}
  if(a==='save-handoff'){state.handoffNote=$('#handoff-note').value;state.pending++;render();toast('留言已保存到手机 · 等待同步');return;}
  if(a==='speak-handoff'){startPlay('照护交接朗读','ask');return;}
  if(a==='share-handoff'){sheet('分享交接预览',`<div class="answer"><p>最近喂奶：19:40，配方奶 150 ml。<br>睡眠：20:58 开始，正在计时。<br>尿布：18:55，尿尿。</p><p>${esc(state.handoffNote)}</p></div><p class="hint">实际应用下一步打开系统分享菜单，接收者由你选择。</p><button class="primary-button" data-action="share-preview-done">确认预览</button>`);return;}
  if(a==='share-preview-done'){closeSheet();toast('系统分享交接点 · 原型不会发送');return;}
  if(a==='add-reminder'){sheet('新建提醒',`<div class="form-group"><label for="reminder-title">提醒内容</label><input id="reminder-title" placeholder="比如准备晚间奶"></div><div class="form-group"><label for="reminder-time">提醒时间</label><input id="reminder-time" type="datetime-local" value="2026-10-02T22:30"></div><button class="primary-button" data-action="save-reminder">保存提醒</button>`);return;}
  if(a==='save-reminder'){const title=$('#reminder-title').value.trim();if(!title){toast('请写下提醒内容');return;}state.reminders.push({title,time:$('#reminder-time').value.slice(11),done:false});closeSheet();render();toast('提醒已保存 · 通知权限尚未启用');return;}
  if(a==='manage-reminder'){const r=state.reminders[button.dataset.id];state.lastSelected=Number(button.dataset.id);sheet(r.title,`<p class="sheet-subtitle">今天 ${r.time}</p><button class="primary-button" data-action="complete-reminder">${icon('check')}标记完成</button><button class="secondary-button" data-action="snooze-reminder" style="margin-top:10px">${icon('alarm-clock')}十分钟后再提醒</button><button class="text-button" data-action="delete-reminder">删除提醒</button>`);return;}
  if(a==='complete-reminder'){state.reminders[state.lastSelected].done=true;closeSheet();render();toast('提醒已完成');return;}
  if(a==='snooze-reminder'){closeSheet();toast('已延后十分钟 · 状态演示');return;}
  if(a==='delete-reminder'){state.reminders.splice(state.lastSelected,1);closeSheet();render();toast('提醒已删除');return;}
  if(a==='notification-permission'){toast('实际应用进入 Android 通知权限说明与系统授权');return;}
  if(a==='export-format'){state.exportFormat=v;render();return;}
  if(a==='export-preview'){sheet('导出预览',`<div class="group"><div class="row"><span class="row-content"><b>2026/09/26 – 2026/10/02</b><small>Asia/Shanghai · ${state.exportFormat}</small></span></div><div class="row"><span class="row-content"><b>${state.empty?0:state.events.length} 条示例记录</b><small>${state.pending} 项待同步 · 保留同步状态列</small></span></div></div><p class="hint">这里展示文件生成前的核对状态。原型不会写出真实数据文件。</p><button class="primary-button" data-action="export-done">${icon('share-2')}保存或分享文件</button>`);return;}
  if(a==='export-done'){closeSheet();toast('系统文件分享交接点 · 原型未生成文件');return;}
  if(a==='save-profile'){toast('资料保存状态演示 · 不改变真实档案');return;}
  if(a==='sync-now'){toast(state.offline?'当前离线，所有操作继续保留':'尚未连接家庭服务，请先设置连接');return;}
  if(a==='simulate-conflict'){sheet('这条记录在另一端修改过',`<p class="sheet-subtitle">自动覆盖已暂停。两份内容都保留，选定版本后再提交。</p><div class="group"><div class="row"><span class="row-content"><b>本机：150 ml 配方奶</b><small>基础修订 12 · 待提交</small></span></div><div class="row"><span class="row-content"><b>服务端：160 ml 配方奶</b><small>当前修订 13 · 网页修改</small></span></div></div><div class="sheet-footer"><button class="secondary-button" data-action="conflict-resolve" data-value="服务端">采用服务端</button><button class="primary-button" data-action="conflict-resolve" data-value="本机">保留本机，重新提交</button></div>`);return;}
  if(a==='conflict-resolve'){closeSheet();toast(`已选择${v}版本 · 冲突状态演示`);return;}
  if(a==='event-detail'){state.lastSelected=Number(button.dataset.id);const event=state.events.find(x=>x.id===state.lastSelected);sheet('记录详情',`<div class="group"><div class="row"><span class="row-content"><b>${esc(event.title)}</b><small>今天 ${event.time} · ${esc(event.note)}</small></span></div></div><p class="hint">${event.pending?'已保存到手机 · 等待同步':'示例已同步状态'}</p><button class="primary-button" data-action="edit-event">${icon('pencil')}编辑记录</button><button class="text-button" data-action="delete-event" style="color:var(--danger)">${icon('trash-2')}删除记录</button>`);return;}
  if(a==='edit-event'){const event=state.events.find(x=>x.id===state.lastSelected);if(event.kind==='feed'){state.feedAmount=Number((event.title.match(/[0-9]+/)||[150])[0]);feedSheet(true);}else if(event.kind==='sleep'){closeSheet();go('sleep');toast('实际应用在同一事件中编辑起止时间');}else{diaperSheet();toast('实际应用保存为原记录的修订');}return;}
  if(a==='delete-event'){sheet('删除这条记录？',`<p class="sheet-subtitle">本机删除会排队同步到其他端。删除后可以从最近删除恢复。</p><div class="sheet-footer"><button class="secondary-button" data-action="close-sheet">保留记录</button><button class="danger-button" data-action="confirm-delete-event">删除记录</button></div>`);return;}
  if(a==='confirm-delete-event'){state.lastDeleted=state.events.find(x=>x.id===state.lastSelected);state.events=state.events.filter(x=>x.id!==state.lastSelected);state.pending++;closeSheet();render();toast('已删除 · 删除操作待同步','恢复','restore-event');return;}
  if(a==='restore-event'){if(state.lastDeleted)state.events.unshift(state.lastDeleted);state.pending++;state.lastDeleted=null;render();toast('记录已恢复 · 等待同步');return;}
  if(a==='extra'){sheet(`记录${v}`,`<div class="form-group"><label for="record-time">发生时间</label><input id="record-time" type="datetime-local" value="2026-10-02T21:40"></div>${v==='趴趴练习'?'<div class="form-group"><label for="extra-duration">时长（分钟）</label><input id="extra-duration" type="number" inputmode="numeric" value="5"></div>':''}<div class="form-group"><label for="record-note">备注</label><input id="record-note" placeholder="今天的小进步…"></div><button class="primary-button" data-action="save-extra" data-value="${v}">保存记录</button>`);return;}
  if(a==='save-extra'){saveEvent('extra',v,$('#record-note').value||'无备注');return;}
  if(a==='connection-settings'){sheet('家庭服务连接',`<div class="form-group"><label for="service-url">服务地址</label><input id="service-url" type="url" placeholder="https://你的家庭服务地址"></div><div class="form-group"><label for="service-token">访问凭据</label><input id="service-token" type="password" placeholder="粘贴访问凭据" autocomplete="off"></div>${notice('凭据不会出现在记录、导出或日志中。连接前核对服务支持的 API 版本。')}<button class="primary-button" data-action="test-connection">检查连接</button>`);return;}
  if(a==='test-connection'){toast('连接检查界面演示 · 原型不会读取或发送凭据');return;}
  if(a==='permissions'){sheet('麦克风权限',`<p class="sheet-subtitle">只有选择语音输入时才请求权限。拒绝后仍可以打字记录和提问。</p><button class="primary-button" data-action="open-system-settings">打开系统设置</button>`);return;}
  if(a==='cache-settings'){sheet('下载与缓存',`<div class="stats-grid"><div class="stat"><span>已下载</span><b>12<small>MB</small></b></div><div class="stat"><span>缓存上限</span><b>256<small>MB</small></b></div><div class="stat"><span>自动清理</span><b style="font-size:18px">已开启</b></div></div><p class="hint">最久未使用的播放缓存先清理，收藏文字和照护记录保留。手动下载使用独立配额。</p><button class="secondary-button" data-action="clear-cache">清理播放缓存</button>`);return;}
  if(a==='clear-cache'){closeSheet();toast('缓存清理状态演示 · 记录与收藏保留');return;}
  if(a==='device-settings'){sheet('配套设备',`<div class="empty"><div class="empty-icon">${icon('bluetooth')}</div><b>还没有连接硬件</b><p>手机可独立记录、语音输入和播放。<br>确需配套设备时再添加。</p></div><p class="hint">未来设备接入需要独立的配对与现场验证；当前原型没有 BLE 功能。</p>`);return;}
}
function startPlay(title,kind){state.player={title,kind};state.playback=true;closeSheet();if(state.page==='player')render();else{updatePlayer();icons();}toast('播放状态演示 · 原型没有真实声音');}
document.addEventListener('DOMContentLoaded',initialize);

/* Revision 2: one conversation entry, records, household. */
const pages=[['home','照护 · 统一对话','message-circle'],['history','记录 · 查阅','notebook-pen'],['settings','家庭 · 人物与维护','users-round']];
const care={draft:'',messages:[],profile:{id:'baby-local',name:'小西瓜',birthday:'',note:'',revision:1},members:[],actor:'',personDraft:null,sequence:0};
function initialize(){
  try{const saved=JSON.parse(localStorage.getItem('xigua-design-family-v2')||'null');if(saved){care.profile=saved.profile;care.members=saved.members||[];care.actor=saved.actor||'';}}catch{}
  state.empty=true;state.events=[];state.sleep=false;state.pending=0;state.reminders=[];state.handoffNote='';
  $('#studio-pages').innerHTML=pages.map(([p,label,i])=>`<button data-page="${p}">${icon(i)}${label}</button>`).join('');
  $('#bottom-nav').innerHTML=[['home','照护','message-circle'],['history','记录','notebook-pen'],['settings','家庭','users-round']].map(([p,t,i])=>`<button data-page="${p}"><span class="nav-icon">${icon(i)}</span>${t}</button>`).join('');
  document.addEventListener('click',onClick);
  $('#offline-demo').addEventListener('change',e=>{state.offline=e.target.checked;render();});
  $('#permission-demo').addEventListener('change',e=>{state.permission=e.target.checked;});
  $('#failure-demo').addEventListener('change',e=>{state.failNext=e.target.checked;});
  $('#empty-demo').addEventListener('change',e=>{state.empty=!e.target.checked;state.events=state.empty?[]:sampleEvents.map(x=>({...x}));state.sleep=!state.empty;state.pending=state.empty?0:2;render();});
  $('#large-demo').addEventListener('change',e=>{state.large=e.target.checked;render();});
  document.addEventListener('input',e=>{if(e.target.id==='care-input')care.draft=e.target.value;});render();
}
function navGroup(){return ['profile','maintenance','sync'].includes(state.page)?'settings':['library','player'].includes(state.page)?'home':['export','sleep'].includes(state.page)?'history':state.page;}
function render(){
  const conversationOffset=$('.conversation-scroll')?.scrollTop||0;
  const p=state.page;$('#phone').classList.toggle('night',state.night);$('#phone').classList.toggle('large-type',state.large);
  $('.theme-button span').textContent=state.night?'日间模式':'夜间模式';
  $('#studio-pages').querySelectorAll('[data-page]').forEach(el=>el.classList.toggle('active',el.dataset.page===navGroup()));
  $('#bottom-nav').querySelectorAll('[data-page]').forEach(el=>el.classList.toggle('active',el.dataset.page===navGroup()));
  const desc={home:['只需说一句','记录、查阅、问答、故事、声音和提醒由意图分流。涉及写入先确认；每个任务保留自己的身份与恢复状态。'],settings:['先把家里的人登记好','本地人物档案可以登记、切换、更新和停用；不等于云账号注册。维护状态如实显示，不伪装成在线服务。'],history:['记录仍然随手可查','当天摘要与历史从同一本地数据源读取，修改、删除、导出都保留明确去向。'],maintenance:['长期运行，要能看见状态','本机版本、更新渠道、同步、空间、备份和恢复都有下一步；未配置的线上能力清楚标注。'],profile:['更新资料，不覆盖历史','宝宝昵称与生日可修正，修改有版本；未知生日不猜年龄。照护者身份不会改变宝宝数据归属。']};
  const d=desc[p]||desc.home;$('#design-heading').textContent=d[0];$('#design-description').textContent=d[1];
  const views={home:conversationView,history:historyView,settings:familyView,profile:babyProfileView,maintenance:maintenanceView,sync:serviceView,library:libraryView,player:playerView,export:exportView,sleep:sleepView};
  $('#screen').classList.toggle('care-screen',p==='home');
  $('#screen').innerHTML=(views[p]||conversationView)();if(p==='home'&&$('.conversation-scroll'))$('.conversation-scroll').scrollTop=conversationOffset;updatePlayer();icons();
}
function caregiver(){const p=care.members.find(x=>x.id===care.actor&&!x.archived);return p?p.name:'未登记照护者';}
function conversationView(){
  $('#app-header').innerHTML=header('一起照顾 '+esc(care.profile.name),'10 月 3 日 · 记录人：'+esc(caregiver()),'profile');
  return `<div class="conversation-scroll">${syncText()}<div class="care-context">${icon(state.sleep?'moon':'heart')}<div><b>${state.sleep?'正在睡觉 · 00:42':'今天，按你们的节奏来'}</b><small>${state.sleep?'20:58 开始 · 切页继续计时':'记录、提问、讲故事，都从这里开始'}</small></div>${state.sleep?'<button class="text-button" data-action="utterance" data-value="宝宝醒了">结束</button>':''}</div>
  ${care.messages.length?care.messages.slice(-10).map(messageView).join(''):`<div class="conversation-intro"><div class="conversation-emblem">${icon('sparkles')}</div><h2>想记、想问、想听，<br>说一句就好。</h2><p>我先帮你整理意图，<br>需要保存的内容，由你确认。</p></div>`}
  <div class="suggestions">${['喝了150毫升配方奶','宝宝睡了','讲个晚安故事','今天喝了多少奶'].map(t=>`<button class="suggestion" data-action="fill-care" data-value="${t}">${t}</button>`).join('')}</div>
  </div><div class="composer unified-composer"><textarea id="care-input" aria-label="统一对话文字" placeholder="记一件事，或说说你需要什么…">${esc(care.draft)}</textarea><div class="composer-foot"><button class="text-button" data-action="voice-unified">${icon('mic')}语音输入</button><button class="primary-button" data-action="send-care">发送${icon('arrow-up')}</button></div></div><p class="hint">本机记录与查询可离线。在线问答/故事需实际服务可用。</p>`;
}
function messageView(m){
  const label={feed:'喂奶记录',sleep:'睡眠记录',diaper:'尿布记录',query:'本机查询',ask:'问答',story:'故事',audio:'声音',reminder:'提醒',person:'人物资料',clarify:'请补充'}[m.type]||'请再说明';
  let actions='';if(m.status==='confirm')actions=`<div class="intent-actions"><button class="secondary-button" data-action="dismiss-intent" data-id="${m.id}">取消</button><button class="primary-button" data-action="commit-intent" data-id="${m.id}">确认保存</button></div>`;
  if(m.status==='blocked')actions=`<button class="secondary-button" data-page="sync">查看服务状态</button><button class="text-button" data-action="demo-ai" data-id="${m.id}">仅预览回复样式</button>`;
  if(m.status==='demo')actions=`<button class="secondary-button" data-action="play-demo" data-id="${m.id}">${icon('volume-2')}朗读状态演示</button>`;
  if(m.status==='audio')actions=`<button class="secondary-button" data-action="play" data-value="轻柔雨声">${icon('play')}播放轻雨（状态演示）</button><button class="text-button" data-page="library">选择其他声音</button>`;
  return `<div class="question-bubble">${esc(m.prompt)}</div><article class="intent-card" data-message-id="${m.id}"><span class="pill">${label} · ${m.status==='confirm'?'待确认':m.status==='saved'?'已存本机':m.status==='blocked'?'服务未连接':m.status==='demo'?'界面演示':m.status==='cancelled'?'已取消':m.status==='failed'?'保存失败':'本机结果'}</span><p>${esc(m.result).replace(/\n/g,'<br>')}</p>${actions}${m.status==='failed'?`<button class="primary-button" data-action="commit-intent" data-id="${m.id}">重试保存</button>`:''}</article>`;
}
function submitCare(text){
  const prompt=text.trim();if(!prompt){toast('先说一句或输入文字');return;}
  const m={id:++care.sequence,prompt,profileId:care.profile.id,profileName:care.profile.name,actorId:care.actor,actorName:caregiver(),type:'ask',status:'blocked',result:'问题已保留。尚未连接在线服务，当前不会发送。'};
  const mutations=[/喝|喂奶/.test(prompt)&&/(\d+)\s*(毫升|ml)/i.test(prompt),/宝宝睡了|开始睡|醒了|结束睡/.test(prompt),/尿布|尿尿|便便/.test(prompt)];
  if(mutations.filter(Boolean).length>1){m.type='clarify';m.status='complete';m.result='这句话包含多个操作。请先选择要记录的一件事，再确认。尚未保存任何内容。';}
  else if(/喝|喂奶/.test(prompt)&&/(\d+)\s*(毫升|ml)/i.test(prompt)){m.type='feed';m.amount=Number(prompt.match(/(\d+)\s*(毫升|ml)/i)[1]);m.status='confirm';m.result=`将记录喂奶 ${m.amount} ml · 配方奶\n宝宝 ${m.profileName} · 记录人 ${m.actorName}\n今天 21:40 · 确认后先保存到手机。`;}
  else if(/宝宝睡了|开始睡/.test(prompt)){m.type='sleep';m.start=true;m.status='confirm';m.result='将开始睡眠计时 · 今天 21:40\n退出页面仍保留，尚未写入。';}
  else if(/醒了|结束睡/.test(prompt)){m.type='sleep';m.start=false;m.status=state.sleep?'confirm':'complete';m.result=state.sleep?'将结束当前睡眠，保存准确起止时间。':'当前没有进行中的睡眠。可以补记一段睡眠。';}
  else if(/尿布|尿尿|便便/.test(prompt)){m.type='diaper';m.status='confirm';m.diaper=/便/.test(prompt)?'便便':'尿尿';m.result=`将记录尿布 · ${m.diaper}\n今天 21:40 · 确认后保存。`;}
  else if(/今天|多少|几次|交接|整理/.test(prompt)&&!/故事/.test(prompt)){m.type='query';m.status='complete';const t=totals();m.result=`本机今日：瓶喂 ${t.ml} ml，尿布 ${t.diapers} 次，完成睡眠 ${(t.sleep/60).toFixed(1)} 小时。\n${state.pending} 项待同步；进行中睡眠另计。`;}
  else if(/故事/.test(prompt)){m.type='story';m.result='故事主题已保留。在线生成需服务连接；尚未发起请求。';}
  else if(/播放|音乐|雨声|声音/.test(prompt)){m.type='audio';m.status='audio';m.result='先选择已在本机可用的声音。在线目录未连接时不伪造曲目。';}
  else if(/提醒/.test(prompt)){m.type='reminder';m.status='complete';m.result='请确认提醒内容和日期时间。原型示范本机登记，不会发出真实通知。';care.messages.push(m);care.draft='';render();sheet('登记提醒','<div class="form-group"><label for="reminder-title">提醒内容</label><input id="reminder-title" value="准备晚间奶"></div><div class="form-group"><label for="reminder-time">时间</label><input id="reminder-time" type="datetime-local" value="2026-10-03T22:30"></div><button class="primary-button" data-action="save-reminder">确认保存</button>');return;}
  else if(/登记|添加.*人|资料|照护者/.test(prompt)){m.type='person';m.status='complete';m.result='人物资料由本机登记表确认，不通过模型直接更新。';care.messages.push(m);care.draft='';render();personForm();return;}
  care.messages.push(m);care.draft='';render();$(`[data-message-id="${m.id}"]`)?.scrollIntoView({block:'nearest'});
}
function familyView(){
  $('#app-header').innerHTML=header('我们的家','人物资料与应用维护','none');
  return `<div class="settings-profile"><span class="profile-button">${esc(care.profile.name.slice(0,1))}</span><span><b>${esc(care.profile.name)}</b><small>${care.profile.birthday?'生日 '+care.profile.birthday:'生日未填写'} · 宝宝档案</small></span><button class="text-button" data-page="profile">更新资料</button></div>${section('照护者','登记人物','settings')}<p class="hint">这是本机人物登记，不是云账号。记录宝宝数据时可注明照护者。</p><div class="group">${care.members.filter(x=>!x.archived).map(p=>`<button class="row" data-action="person-detail" data-id="${p.id}"><span class="row-icon">${icon('user-round')}</span><span class="row-content"><b>${esc(p.name)} · ${esc(p.relation)}</b><small>资料版本 ${p.revision} · ${p.id===care.actor?'当前记录人':'仅本机保存'}</small></span>${icon('chevron-right')}</button>`).join('')||empty('还没有登记照护者','可以先使用，再随时补充人物资料。')}</div><button class="secondary-button" data-action="register-person">${icon('user-round-plus')}登记照护者</button>${section('应用与数据')}<div class="group"><button class="row" data-page="maintenance"><span class="row-icon">${icon('refresh-cw')}</span><span class="row-content"><b>版本与长期维护</b><small>更新渠道、备份、恢复与存储</small></span>${icon('chevron-right')}</button><button class="row" data-page="sync"><span class="row-icon">${icon('cloud')}</span><span class="row-content"><b>家庭服务与同步</b><small>${state.pending} 项待同步 · 服务未配置</small></span>${icon('chevron-right')}</button><button class="row" data-action="theme"><span class="row-icon">${icon('moon')}</span><span class="row-content"><b>夜间模式</b><small>深夜也能清楚阅读</small></span><span class="toggle ${state.night?'on':''}"></span></button><button class="row" data-action="large"><span class="row-icon">${icon('type')}</span><span class="row-content"><b>大字阅读</b><small>尊重系统字体缩放</small></span><span class="toggle ${state.large?'on':''}"></span></button></div>${notice('云账号注册、家庭邀请与自动在线升级尚无可用服务，不展示成已开通功能。')}`;
}
function babyProfileView(){
  $('#app-header').innerHTML=header('宝宝资料','版本 '+care.profile.revision+' · 仅本机','none',true);
  return `<div class="form-group"><label for="baby-name">昵称</label><input id="baby-name" value="${esc(care.profile.name)}" maxlength="32"></div><div class="form-group"><label for="baby-birthday">生日（可不填）</label><input id="baby-birthday" type="date" value="${care.profile.birthday}" max="2026-10-03"></div><div class="form-group"><label for="baby-note">照护偏好（可选）</label><textarea id="baby-note" placeholder="入睡习惯、家里常用称呼…">${esc(care.profile.note)}</textarea></div>${notice('更新昵称与资料不会重新创建宝宝，也不会改变已有记录。生日未知时不猜年龄。')}<button class="primary-button" data-action="review-baby">预览更新</button>`;
}
function personForm(person=null){
  care.personDraft=person?{...person}:null;
  sheet(person?'更新人物资料':'登记照护者',`<div class="form-group"><label for="person-name">姓名或称呼 *</label><input id="person-name" maxlength="32" value="${person?esc(person.name):''}" placeholder="比如妈妈、爸爸或照护者称呼"></div><div class="form-group"><label for="person-relation">与宝宝的关系 *</label><select id="person-relation">${['妈妈','爸爸','祖辈','其他照护者'].map(r=>`<option ${person&&person.relation===r?'selected':''}>${r}</option>`).join('')}</select></div><div class="form-group"><label for="person-note">备注（可选）</label><textarea id="person-note">${person?esc(person.note||''):''}</textarea></div><p class="hint">只登记本机资料，不创建登录账号或授予云权限。</p><button class="primary-button" data-action="review-person">确认资料</button>`);
}
function persistFamily(){localStorage.setItem('xigua-design-family-v2',JSON.stringify({profile:care.profile,members:care.members,actor:care.actor}));}
function maintenanceView(){
  $('#app-header').innerHTML=header('版本与维护','长期使用，状态一直清楚','none',true);
  return `${section('App 版本')}<div class="group"><div class="row"><span class="row-content"><b>0.1.0-local（示例版本）</b><small>本地构建 · 独立育儿包身份</small></span><span class="pill">本机</span></div><div class="row"><span class="row-content"><b>在线更新渠道未配置</b><small>上次检查：未检查 · 不推断已经最新版</small></span></div></div><button class="secondary-button" data-action="update-unavailable">查看更新方式</button>${section('数据与恢复')}<div class="group"><button class="row" data-action="backup-preview"><span class="row-icon">${icon('download')}</span><span class="row-content"><b>导出本机备份</b><small>人物资料与记录 · 含待同步状态</small></span>${icon('chevron-right')}</button><button class="row" data-action="restore-explained"><span class="row-icon">${icon('archive-restore')}</span><span class="row-content"><b>恢复备份</b><small>先预览、检查版本，再由你确认</small></span>${icon('chevron-right')}</button><button class="row" data-action="cache-settings"><span class="row-icon">${icon('hard-drive')}</span><span class="row-content"><b>空间与缓存</b><small>只清缓存，不自动删除照护历史</small></span>${icon('chevron-right')}</button></div>${notice('应用升级保留人物 ID、历史与离线队列。迁移失败要保留旧数据并提供恢复；在线服务不长期连接也能使用本机记录。')}`;
}
function onClick(e){
  const b=e.target.closest('button');if(!b)return;
  if(b.dataset.page){if(b.dataset.page==='settings'&&b.textContent.includes('登记人物')){personForm();return;}go(b.dataset.page);return;}
  const a=b.dataset.action,id=b.dataset.id;
  if(a==='back'){go(['profile','maintenance','sync'].includes(state.page)?'settings':['export','sleep'].includes(state.page)?'history':'home');return;}
  if(a==='start-sleep'||a==='end-sleep'){go('home');submitCare(a==='start-sleep'?'宝宝睡了':'宝宝醒了');return;}
  if(a==='fill-care'){care.draft=b.dataset.value;render();return;}
  if(a==='utterance'){submitCare(b.dataset.value);return;}
  if(a==='send-care'){submitCare($('#care-input').value);return;}
  if(a==='voice-unified'){voiceSheet('unified');return;}
  if(a==='confirm-voice'&&state.voiceMode==='unified'){care.draft=$('#voice-transcript').value;closeSheet();go('home');toast('文字已填入 · 还未发送或保存');return;}
  if(a==='voice-type'&&state.voiceMode==='unified'){closeSheet();go('home');return;}
  if(a==='dismiss-intent'){care.messages.find(m=>m.id===Number(id)).status='cancelled';render();return;}
  if(a==='commit-intent'){
    const m=care.messages.find(x=>x.id===Number(id));if(!m||m.status==='saved')return;
    if(state.failNext){state.failNext=false;$('#failure-demo').checked=false;m.status='failed';m.result+='\n未保存，输入已保留。';render();return;}
    if(m.type==='feed'){state.events.unshift({id:Date.now(),kind:'feed',time:'21:40',title:`喂奶 · ${m.amount} ml`,note:`配方奶 · ${m.actorName}`,profileId:m.profileId,actorId:m.actorId,pending:true});}
    if(m.type==='sleep'){state.sleep=m.start;if(!m.start)state.events.unshift({id:Date.now(),kind:'sleep',time:'21:40',title:'睡眠 · 42 分钟',note:'起止时间保存',pending:true,minutes:42});}
    if(m.type==='diaper')state.events.unshift({id:Date.now(),kind:'diaper',time:'21:40',title:'尿布 · '+m.diaper,note:m.actorName,profileId:m.profileId,actorId:m.actorId,pending:true});
    state.empty=false;state.pending++;m.status='saved';m.result='已保存到手机 · 等待同步。此处为设计示例数据。';render();return;
  }
  if(a==='demo-ai'){const m=care.messages.find(x=>x.id===Number(id));m.status='demo';m.result=m.type==='story'?'示例故事：小熊把一盏月亮灯放到窗边，轻轻说晚安。\n仅预览文字布局，未调用在线模型。':'示例回答：我可以帮你整理记录与问题。\n仅预览布局，不作为真实AI结果。';render();return;}
  if(a==='play-demo'){startPlay('对话回复朗读 · 状态演示','story');return;}
  if(a==='register-person'){personForm();return;}
  if(a==='review-person'){
    const name=$('#person-name').value.trim();if(!name){toast('请填写称呼，资料尚未保存');return;}
    care.personDraft={...(care.personDraft||{}),name,relation:$('#person-relation').value,note:$('#person-note').value,archived:false};
    sheet(care.personDraft.id?'确认人物资料更新':'确认本机人物登记',`<div class="answer"><h3>${esc(name)}</h3><p>${esc(care.personDraft.relation)} · 仅本机档案<br>${esc(care.personDraft.note||'无备注')}</p></div><p class="hint">保存后可选为记录人。不是云账号，不产生邀请或权限。</p><p id="person-save-state" class="hint" role="alert"></p><div class="sheet-footer"><button class="secondary-button" data-action="edit-person-draft">返回修改</button><button class="primary-button" data-action="save-person">保存人物</button></div>`);return;
  }
  if(a==='edit-person-draft'){personForm(care.personDraft);return;}
  if(a==='save-person'){
    if(state.failNext){state.failNext=false;$('#failure-demo').checked=false;$('#person-save-state').textContent='保存失败，资料草稿保留。请重试或返回修改。';toast('保存失败，资料草稿保留 · 可重试');return;}
    const draft=care.personDraft;if(!draft)return;const d={...draft,id:draft.id||'person-'+Date.now(),revision:(draft.revision||0)+1};const next=care.members.map(x=>({...x}));const prev=next.findIndex(x=>x.id===d.id);
    if(prev<0)next.push(d);else next[prev]=d;
    try{localStorage.setItem('xigua-design-family-v2',JSON.stringify({profile:care.profile,members:next,actor:d.id}));care.members=next;care.actor=d.id;closeSheet();render();toast('人物已保存到此浏览器 · App/云端未修改');}catch{$('#person-save-state').textContent='浏览器存储失败，原资料未改变，草稿保留。请重试。';}return;
  }
  if(a==='person-detail'){const p=care.members.find(x=>x.id===id);care.personDraft={...p};sheet('人物资料',`<div class="answer"><h3>${esc(p.name)}</h3><p>${esc(p.relation)} · 资料版本 ${p.revision}<br>${esc(p.note||'无备注')}</p></div><button class="primary-button" data-action="select-person">设为当前记录人</button><button class="secondary-button" data-action="edit-person-draft" style="margin-top:10px">更新资料</button><button class="text-button" data-action="archive-person">停用此人物</button>`);return;}
  if(a==='select-person'){care.actor=care.personDraft.id;persistFamily();closeSheet();render();toast('记录人已切换 · 不改变宝宝或历史');return;}
  if(a==='archive-person'){sheet('停用此人物？','<p class="sheet-subtitle">历史记录保留人物身份。停用后不再供新记录选择；不会删除历史或撤销云权限。</p><div class="sheet-footer"><button class="secondary-button" data-action="close-sheet">保留</button><button class="danger-button" data-action="confirm-archive-person">确认停用</button></div>');return;}
  if(a==='confirm-archive-person'){care.members.find(x=>x.id===care.personDraft.id).archived=true;if(care.actor===care.personDraft.id)care.actor='';persistFamily();closeSheet();render();toast('人物已停用 · 历史保留');return;}
  if(a==='review-baby'){const name=$('#baby-name').value.trim(),birthday=$('#baby-birthday').value;if(!name){toast('昵称不能为空');return;}if(birthday>'2026-10-03'){toast('生日不能是未来日期');return;}care.babyDraft={...care.profile,name,birthday,note:$('#baby-note').value};sheet('确认宝宝资料更新',`<div class="answer"><h3>${esc(name)}</h3><p>生日：${birthday||'未填写'}<br>宝宝 ID 保持，历史记录保留。</p></div><button class="primary-button" data-action="save-baby">确认更新</button>`);return;}
  if(a==='save-baby'){if(state.failNext){state.failNext=false;$('#failure-demo').checked=false;toast('更新失败，原资料与草稿保留');return;}care.profile={...care.babyDraft,revision:care.profile.revision+1};persistFamily();closeSheet();render();toast('资料版本已更新 · 仅此浏览器');return;}
  if(a==='update-unavailable'){sheet('更新渠道未配置','<p class="sheet-subtitle">当前不能检查在线版本，也不会声称已经最新版。由可信来源获取同包名、同签名且更高版本的 APK，再经系统安装；先导出备份。</p><button class="secondary-button" data-action="backup-preview">查看本机备份内容</button>');return;}
  if(a==='backup-preview'){sheet('本机备份预览',`<div class="answer"><p>人物 ${care.members.length+1} 个 · 示例记录 ${state.events.length} 条<br>待同步 ${state.pending} 项 · 时区 Asia/Shanghai<br>不包含访问凭据。</p></div><p class="hint">当前只演示预览。真实 App 需流式导出、校验和系统文件保存。</p>`);return;}
  if(a==='restore-explained'){sheet('恢复前先核对','<div class="answer"><p>1. 选择本机备份，校验格式与完整性。<br>2. 预览人物/记录/修订及冲突。<br>3. 明确合并或替换方式，再确认。<br>4. 保留恢复前备份与失败回退。</p></div><p class="hint">当前原型不执行恢复，不制造已经恢复的提示。清空代次不能被旧备份绕过。</p>');return;}
  if(a==='reset'){localStorage.removeItem('xigua-design-family-v2');location.reload();return;}
  legacyClick(e);
}

function serviceView(){
  $('#app-header').innerHTML=header('家庭服务与同步','本机可用，线上能力按实际状态开放','none',true);
  const pending=state.events.filter(e=>e.pending);
  return `<div class="hero"><div class="kicker">${icon('cloud-off')}服务未连接</div><h2>记录可以继续。<br>在线问答与故事暂不可用。</h2><p class="hero-foot">不会发送输入，也不会把未确认操作标为同步成功。</p></div>${notice('当前为设计原型，未调用后端。后端契约仍需实际验证；填写地址不代表连接成功。')}${section('本机状态')}<div class="group"><div class="row"><span class="row-content"><b>人物档案</b><small>仅保存在此浏览器 · 未同步到 App/云端</small></span><span class="pill">本机</span></div><div class="row"><span class="row-content"><b>${state.pending} 项示例操作待同步</b><small>服务确认之前保留待处理状态</small></span></div></div>${section('待同步记录示例')}<div class="group">${pending.length?pending.map(e=>eventRow(e)).join(''):empty('没有待同步记录','开始睡眠等状态操作可能单独占用待处理队列。')}</div><p class="hint">接入服务后再开放经过验证的地址/凭据配置、连接检测与重试。云账号注册、邀请和在线升级需要单独的真实契约。</p>`;
}
