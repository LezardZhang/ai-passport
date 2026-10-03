package cn.xigua.childcare;

import android.app.*;
import android.content.*;
import android.media.*;
import android.media.session.*;
import android.net.Uri;
import android.os.*;
import android.speech.tts.*;
import java.io.File;
import java.util.*;

/** Exactly one audio owner. Streams use the platform decoder; no whole-media arrays. */
public final class PlaybackService extends Service {
    public static volatile String title="",state="idle",source="",error="";
    private static PlaybackService instance;
    private MediaPlayer player;private MediaSession session;private AudioManager audio;private AudioFocusRequest focus;private TextToSpeech tts;
    private final Handler h=new Handler(Looper.getMainLooper());private int generation;private boolean resumeOnFocus,loop,prepared,desiredPlay=true;private long stopAt;private File speechFile;
    private final ArrayDeque<String> speech=new ArrayDeque<>();
    private final Runnable stopTimer=()->stopPlayback();
    private final Runnable checkpoint=new Runnable(){public void run(){if(player!=null&&prepared&&("playing".equals(state)||"paused".equals(state))){remember();h.postDelayed(this,10000);}}};
    private final Runnable pauseExpiry=()->{release();state="stopped";notifyUi();stopForeground(STOP_FOREGROUND_REMOVE);stopSelf();};
    private final BroadcastReceiver noisy=new BroadcastReceiver(){public void onReceive(Context c,Intent i){pause();}};
    static void play(Context c,String name,String url,boolean repeat){Intent i=new Intent(c,PlaybackService.class).setAction("play").putExtra("title",name).putExtra("url",url).putExtra("loop",repeat);c.startForegroundService(i);}
    static void speak(Context c,String name,String text){c.startForegroundService(new Intent(c,PlaybackService.class).setAction("speak").putExtra("title",name).putExtra("text",text));}
    static void control(Context c,String action){if(instance==null){if("resume".equals(action)){if("speech".equals(c.getSharedPreferences("player",0).getString("mode",""))){speak(c,c.getSharedPreferences("player",0).getString("title","朗读"),c.getSharedPreferences("player",0).getString("speech_text",""));return;}String url=c.getSharedPreferences("player",0).getString("url","");if(!url.isEmpty())c.startForegroundService(new Intent(c,PlaybackService.class).setAction("resume"));}return;}c.startService(new Intent(c,PlaybackService.class).setAction(action));}
    static void timer(Context c,int minutes){if(instance!=null)c.startService(new Intent(c,PlaybackService.class).setAction("timer").putExtra("minutes",minutes));}
    @Override public void onCreate(){super.onCreate();instance=this;audio=getSystemService(AudioManager.class);NotificationChannel channel=new NotificationChannel("playback","音频播放",NotificationManager.IMPORTANCE_LOW);getSystemService(NotificationManager.class).createNotificationChannel(channel);
        session=new MediaSession(this,"XiguaChildcare");session.setCallback(new MediaSession.Callback(){public void onPlay(){resume();}public void onPause(){pause();}public void onStop(){stopPlayback();}public void onSeekTo(long pos){if(player!=null&&("playing".equals(state)||"paused".equals(state)))player.seekTo((int)Math.max(0,pos));}});session.setActive(true);
        if(Build.VERSION.SDK_INT>=33)registerReceiver(noisy,new IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY),Context.RECEIVER_NOT_EXPORTED);else registerReceiver(noisy,new IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY));
    }
    @Override public int onStartCommand(Intent i,int flags,int startId){if(i==null){stopSelf();return START_NOT_STICKY;}String action=i.getAction();
        if("play".equals(action)){generation++;clearSpeech();release();desiredPlay=true;title=i.getStringExtra("title");source=i.getStringExtra("url");loop=i.getBooleanExtra("loop",false);getSharedPreferences("player",0).edit().putString("mode","media").putString("speech_text","").apply();error="";state="buffering";foreground();prepare(source,0);}
        else if("speak".equals(action)){generation++;clearSpeech();release();desiredPlay=true;title=i.getStringExtra("title");source="";state="buffering";error="";foreground();String text=i.getStringExtra("text");if(text==null||text.isEmpty()||text.length()>12000){failed("朗读文字为空或超过 12000 字");return START_NOT_STICKY;}
            getSharedPreferences("player",0).edit().putString("mode","speech").putString("speech_text",text).putString("title",title).apply();for(int k=0;k<text.length();k+=3500)speech.add(text.substring(k,Math.min(text.length(),k+3500)));initSpeech();}
        else if("pause".equals(action))pause();else if("resume".equals(action)){foreground();resume();}else if("stop".equals(action))stopPlayback();
        else if("timer".equals(action)){h.removeCallbacks(stopTimer);int min=i.getIntExtra("minutes",0);stopAt=min>0?SystemClock.elapsedRealtime()+min*60000L:0;if(min>0)h.postDelayed(stopTimer,min*60000L);notifyUi();}
        return START_NOT_STICKY;
    }
    private void prepare(String url,int seek){
        if(url==null||url.isEmpty()){failed("音频地址不可用");return;}release();prepared=false;final int owner=generation;final MediaPlayer p=new MediaPlayer();player=p;
        try{p.setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build());p.setWakeMode(this,PowerManager.PARTIAL_WAKE_LOCK);
            if(url.startsWith("https://")){Store s=((CareApp)getApplication()).store;Map<String,String> headers=new HashMap<>();if(CareRules.sameOrigin(s.get("base_url",""),url)&&!s.get("token","").isEmpty())headers.put("Authorization","Bearer "+s.get("token",""));p.setDataSource(this,Uri.parse(url),headers);}
            else if(url.startsWith("http:")){failed("远程音频须使用 HTTPS");return;}else p.setDataSource(url);
            p.setLooping(loop);p.setOnPreparedListener(mp->{if(owner!=generation||mp!=player)return;prepared=true;if(seek>0)mp.seekTo(seek);h.removeCallbacks(checkpoint);h.postDelayed(checkpoint,10000);if(!desiredPlay){state="paused";remember();h.postDelayed(pauseExpiry,5*60000L);update();}else if(acquireFocus()){mp.start();state="playing";remember();update();}else failed("其他应用占用音频，请稍后继续");});
            p.setOnCompletionListener(mp->{if(owner!=generation||mp!=player)return;if(!speech.isEmpty()){release();synthesizeNext();}else{state="complete";release();clearSpeech();update();stopForeground(STOP_FOREGROUND_REMOVE);stopSelf();}});
            p.setOnErrorListener((mp,what,extra)->{if(owner==generation)failed("音频播放失败，请重新选择或重试");return true;});p.setOnInfoListener((mp,what,extra)->{if(owner==generation&&desiredPlay&&(what==MediaPlayer.MEDIA_INFO_BUFFERING_START||what==MediaPlayer.MEDIA_INFO_BUFFERING_END)){state=what==MediaPlayer.MEDIA_INFO_BUFFERING_START?"buffering":"playing";update();}return false;});
            p.prepareAsync();update();
        }catch(Exception e){failed("无法打开音频，请检查文件或连接");}
    }
    private boolean acquireFocus(){if(focus==null)focus=new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN).setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build()).setOnAudioFocusChangeListener(change->{
            if(change==AudioManager.AUDIOFOCUS_GAIN){if(player!=null)player.setVolume(1,1);if(resumeOnFocus){resumeOnFocus=false;resume();}}
            else if(change==AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK){if(player!=null)player.setVolume(.2f,.2f);}
            else {boolean was=desiredPlay&&("playing".equals(state)||"buffering".equals(state));boolean temporary=change==AudioManager.AUDIOFOCUS_LOSS_TRANSIENT;pause(!temporary);resumeOnFocus=temporary&&was;}
        }).build();return audio.requestAudioFocus(focus)==AudioManager.AUDIOFOCUS_REQUEST_GRANTED;}
    private void pause(){pause(true);}
    private void pause(boolean abandon){resumeOnFocus=false;desiredPlay=false;if(abandon&&focus!=null)audio.abandonAudioFocusRequest(focus);if(player!=null&&prepared&&("playing".equals(state)||"buffering".equals(state))){player.pause();state="paused";remember();h.removeCallbacks(pauseExpiry);h.postDelayed(pauseExpiry,5*60000L);update();}
        else if("buffering".equals(state)){state="buffering_paused";h.removeCallbacks(pauseExpiry);h.postDelayed(pauseExpiry,5*60000L);update();}}
    private void resume(){if("failed".equals(state)&&"speech".equals(getSharedPreferences("player",0).getString("mode",""))){speak(this,getSharedPreferences("player",0).getString("title","朗读"),getSharedPreferences("player",0).getString("speech_text",""));return;}h.removeCallbacks(pauseExpiry);if("buffering_paused".equals(state)){desiredPlay=true;state="buffering";update();return;}if("buffering".equals(state)||"playing".equals(state))return;desiredPlay=true;if(player!=null&&prepared&&"paused".equals(state)){if(acquireFocus()){player.start();state="playing";update();}return;}
        if(!PlaybackRules.resumeCreatesPlayer(state,player!=null))return;clearSpeech();release();
        String url=getSharedPreferences("player",0).getString("url","");if(url.isEmpty()){failed("请先选择音频");return;}generation++;source=url;title=getSharedPreferences("player",0).getString("title","音频");loop=getSharedPreferences("player",0).getBoolean("loop",false);state="buffering";prepare(url,getSharedPreferences("player",0).getInt("position",0));}
    private void remember(){if(player!=null&&tts==null&&!source.isEmpty())try{getSharedPreferences("player",0).edit().putString("mode","media").putString("url",source).putString("title",title).putInt("position",player.getCurrentPosition()).putBoolean("loop",loop).apply();}catch(IllegalStateException ignored){}}
    private void initSpeech(){final int owner=generation;tts=new TextToSpeech(this,status->{if(owner!=generation)return;if(status!=TextToSpeech.SUCCESS||tts.setLanguage(Locale.SIMPLIFIED_CHINESE)<0){failed("手机尚未安装中文朗读语音，请在系统文字转语音设置中安装");return;}tts.setSpeechRate(.92f);tts.setOnUtteranceProgressListener(new UtteranceProgressListener(){public void onStart(String id){}public void onDone(String id){h.post(()->{if(owner==generation&&id.equals("speech-"+owner)){if(speechFile.length()>DiskMedia.ITEM_LIMIT){failed("朗读音频超过大小上限");return;}source=speechFile.getAbsolutePath();loop=false;prepare(source,0);}});}public void onError(String id){h.post(()->{if(owner==generation)failed("系统朗读生成失败，请重试");});}});synthesizeNext();});}
    private void synthesizeNext(){if(speechFile!=null)speechFile.delete();speechFile=new File(getCacheDir(),"speech-"+generation+".wav");state=desiredPlay?"buffering":"buffering_paused";update();if(tts.synthesizeToFile(speech.removeFirst(),new Bundle(),speechFile,"speech-"+generation)==TextToSpeech.ERROR)failed("无法生成朗读音频");}
    private void clearSpeech(){speech.clear();if(tts!=null){tts.stop();tts.shutdown();tts=null;}if(speechFile!=null){speechFile.delete();speechFile=null;}}
    private void release(){h.removeCallbacks(pauseExpiry);h.removeCallbacks(checkpoint);prepared=false;if(player!=null){player.release();player=null;}if(focus!=null){audio.abandonAudioFocusRequest(focus);focus=null;}}
    private void stopPlayback(){generation++;resumeOnFocus=false;remember();release();clearSpeech();state="stopped";h.removeCallbacks(stopTimer);stopAt=0;update();stopForeground(STOP_FOREGROUND_REMOVE);stopSelf();}
    private void failed(String message){error=message;state="failed";generation++;release();clearSpeech();update();stopForeground(STOP_FOREGROUND_REMOVE);stopSelf();}
    private PendingIntent action(String a){return PendingIntent.getService(this,a.hashCode(),new Intent(this,PlaybackService.class).setAction(a),PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);}
    private Notification notification(){PendingIntent open=PendingIntent.getActivity(this,0,new Intent(this,MainActivity.class),PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);boolean playing="playing".equals(state)||"buffering".equals(state);
        return new Notification.Builder(this,"playback").setSmallIcon(R.drawable.ic_app).setContentTitle(title==null?"音频":title).setContentText(error.isEmpty()?("paused".equals(state)?"已暂停":"西瓜陪伴 · 可从通知控制"):error).setContentIntent(open).setOngoing(playing).addAction(new Notification.Action.Builder(null,playing?"暂停":"继续",action(playing?"pause":"resume")).build()).addAction(new Notification.Action.Builder(null,"停止",action("stop")).build()).setStyle(new Notification.MediaStyle().setMediaSession(session.getSessionToken()).setShowActionsInCompactView(0,1)).build();}
    private void foreground(){startForeground(77,notification());}
    private void update(){long position=0;try{if(player!=null&&("playing".equals(state)||"paused".equals(state)))position=player.getCurrentPosition();}catch(IllegalStateException ignored){}
        int code="playing".equals(state)?PlaybackState.STATE_PLAYING:"paused".equals(state)?PlaybackState.STATE_PAUSED:"buffering".equals(state)?PlaybackState.STATE_BUFFERING:PlaybackState.STATE_STOPPED;
        session.setPlaybackState(new PlaybackState.Builder().setActions(PlaybackState.ACTION_PLAY|PlaybackState.ACTION_PAUSE|PlaybackState.ACTION_STOP|PlaybackState.ACTION_SEEK_TO).setState(code,position,"playing".equals(state)?1:0).build());session.setMetadata(new MediaMetadata.Builder().putString(MediaMetadata.METADATA_KEY_TITLE,title).build());getSystemService(NotificationManager.class).notify(77,notification());notifyUi();}
    private void notifyUi(){((CareApp)getApplication()).changed();}
    @Override public void onDestroy(){generation++;release();clearSpeech();h.removeCallbacksAndMessages(null);unregisterReceiver(noisy);session.release();instance=null;super.onDestroy();}
    @Override public IBinder onBind(Intent i){return null;}
}
