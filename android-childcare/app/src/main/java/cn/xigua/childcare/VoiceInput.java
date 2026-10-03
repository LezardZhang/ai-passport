package cn.xigua.childcare;
import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;

/** Foreground native voice owner; configuration validation precedes microphone permission/capture. */
final class VoiceInput {
    interface Listener {void state(String status);void transcript(String text);default void phase(SpeechSession.Phase phase){}default void failed(String error){state(error);}default void finished(){}}
    interface BackendFactory {SpeechBackend create()throws Exception;}
    private final Activity activity;private final BackendFactory factory;private Listener activeListener;private MicrophoneCapture capture;private int generation;private boolean permissionPending;
    VoiceInput(Activity a){this(a,()->new MimoClient(a,((CareApp)a.getApplication()).store,"asr"));}
    VoiceInput(Activity a,BackendFactory factory){activity=a;this.factory=factory;}
    boolean active(){return permissionPending||capture!=null;}
    boolean awaitingPermission(){return permissionPending;}
    boolean hasDraft(){return SpeechDrafts.exists(activity);}
    void start(Listener listener){begin(listener,false);}
    void retry(Listener listener){begin(listener,true);}
    private void begin(Listener listener,boolean retry){cancel();activeListener=listener;try{SpeechBackend backend=factory.create();backend.ready();if(!retry&&activity.checkSelfPermission(Manifest.permission.RECORD_AUDIO)!=PackageManager.PERMISSION_GRANTED){permissionPending=true;listener.phase(SpeechSession.Phase.PERMISSION);listener.state("请允许麦克风，允许后会开始录音");activity.requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO},41);return;}startCapture(listener,backend,retry);}catch(Exception e){activeListener=null;listener.failed(e instanceof java.io.IOException?e.getMessage():"语音服务配置不可用，请在AI服务中检查");listener.finished();}}
    private void startCapture(Listener listener,SpeechBackend backend,boolean retry){final int owner=++generation;VoiceInput.Listener guarded=new Listener(){private boolean valid(){return owner==generation;}public void state(String s){if(valid())listener.state(s);}public void transcript(String s){if(valid())listener.transcript(s);}public void phase(SpeechSession.Phase p){if(valid())listener.phase(p);}public void failed(String e){if(valid())listener.failed(e);}};capture=new MicrophoneCapture(activity,guarded,()->{if(owner==generation){capture=null;activeListener=null;listener.finished();}},backend,retry);listener.phase(retry?SpeechSession.Phase.TRANSCRIBING:SpeechSession.Phase.RECORDING);listener.state(retry?"正在重新识别…":"正在启动麦克风…");capture.start();}
    void permissionResult(int[] results){if(!permissionPending)return;permissionPending=false;Listener listener=activeListener;if(listener==null)return;if(results.length==0||results[0]!=PackageManager.PERMISSION_GRANTED){activeListener=null;listener.failed("未允许麦克风，可继续打字，或在系统设置中开启");listener.finished();return;}try{startCapture(listener,factory.create(),false);}catch(Exception e){activeListener=null;listener.failed("语音服务暂时不可用，可检查AI服务后重试");listener.finished();}}
    void stop(){if(capture!=null)capture.stop();}
    void cancel(){generation++;permissionPending=false;Listener old=activeListener;activeListener=null;if(capture!=null){capture.cancel();capture=null;}if(old!=null)old.finished();}
    void discard(){cancel();SpeechDrafts.discard(activity);}
}
