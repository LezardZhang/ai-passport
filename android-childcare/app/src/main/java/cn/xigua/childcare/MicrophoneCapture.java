package cn.xigua.childcare;
import android.app.Activity;
import android.media.*;
import java.io.*;
import java.util.UUID;

/** AudioRecord closes before ASR. Each utterance is single-use and its draft is discarded after processing. */
final class MicrophoneCapture {
    private final Activity activity;private final CareApp app;private final VoiceInput.Listener listener;private final Runnable complete;private final SpeechBackend backend;private final boolean retry;
    private volatile boolean recording=true,cancelled;private AudioRecord recorder;
    MicrophoneCapture(Activity a,VoiceInput.Listener listener,Runnable complete,SpeechBackend backend,boolean retry){activity=a;app=(CareApp)a.getApplication();this.listener=listener;this.complete=complete;this.backend=backend;this.retry=retry;}
    void start(){PlaybackService.control(activity,"pause");app.speech.execute(this::capture);}
    synchronized void stop(){recording=false;if(recorder!=null)try{recorder.stop();}catch(Exception ignored){}}
    synchronized void cancel(){cancelled=true;stop();backend.cancel();}
    private void state(SpeechSession.Phase phase,String text){app.main.post(()->{if(!cancelled){listener.phase(phase);listener.state(text);}});}
    private void capture(){File temporary=new File(activity.getCacheDir(),"capture-"+UUID.randomUUID()+".wav"),scratch=new File(activity.getCacheDir(),"asr-part-"+UUID.randomUUID()+".wav");AudioRecord local=null;try{
        backend.ready();if(cancelled)return;
        if(!retry){if(activity.getCacheDir().getUsableSpace()<8*1024*1024)throw new IOException("手机空间不足，请整理后再录音");int minimum=AudioRecord.getMinBufferSize(16000,AudioFormat.CHANNEL_IN_MONO,AudioFormat.ENCODING_PCM_16BIT);if(minimum<=0)throw new IOException("手机不支持录音格式");local=new AudioRecord(MediaRecorder.AudioSource.VOICE_RECOGNITION,16000,AudioFormat.CHANNEL_IN_MONO,AudioFormat.ENCODING_PCM_16BIT,Math.max(minimum,4096));
            synchronized(this){if(cancelled)return;if(!recording)throw new IOException("录音尚未开始，请重新说一句");recorder=local;if(local.getState()!=AudioRecord.STATE_INITIALIZED)throw new IOException("麦克风无法初始化");local.startRecording();}state(SpeechSession.Phase.RECORDING,"正在听 · 点结束录音可编辑，点发送直接提交");boolean storageStop=false;int size=0;byte[] buffer=new byte[4096];
            try(RandomAccessFile out=new RandomAccessFile(temporary,"rw")){out.write(SpeechAudio.header(0));while(recording&&!cancelled){int n=local.read(buffer,0,buffer.length);if(n<0){if(!recording||cancelled)break;throw new IOException("录音中断，请重试");}if(n==0)continue;if(size+n>SpeechAudio.RECORD_LIMIT||activity.getCacheDir().getUsableSpace()<4*1024*1024){recording=false;storageStop=true;break;}out.write(buffer,0,n);size+=n;}out.seek(0);out.write(SpeechAudio.header(size));}
            synchronized(this){recording=false;recorder=null;try{local.stop();}catch(Exception ignored){}local.release();local=null;}if(cancelled)return;if(size<1600)throw new IOException("录音太短，请重新说一句");SpeechDrafts.keep(activity,temporary);if(storageStop)throw new IOException("录音过长或手机空间不足，请分次录入");
        }
        if(!SpeechDrafts.exists(activity))throw new IOException("没有可重试的录音，请重新说一句");state(SpeechSession.Phase.TRANSCRIBING,"正在转成文字…");String text=backend.transcribe(SpeechDrafts.file(activity),scratch,message->state(SpeechSession.Phase.TRANSCRIBING,message));if(text==null||text.trim().isEmpty())throw new IOException("没有识别到文字，请重新说一句");app.main.post(()->{if(!cancelled){listener.transcript(text);SpeechDrafts.discard(activity);listener.state("已转成文字，可以编辑或发送");}});
    }catch(Exception e){SpeechDrafts.discard(activity);if(!cancelled)app.main.post(()->{if(!cancelled)listener.failed(e instanceof IOException?e.getMessage():"这套服务的识别接口或模型不兼容，可切换后重试");});}finally{synchronized(this){recorder=null;if(local!=null){try{local.stop();}catch(Exception ignored){}local.release();}}temporary.delete();scratch.delete();SpeechDrafts.discard(activity);app.main.post(complete);}}
}
