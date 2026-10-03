package cn.xigua.childcare;
import android.app.*;
import android.os.Bundle;
import java.io.*;
import java.util.UUID;
import org.json.*;
/** Owner-authorized live calls use a generated phrase, no family records or microphone. */
public final class LiveProviderInstrumentation extends Instrumentation {
 @Override public void onCreate(Bundle b){super.onCreate(b);start();}
 @Override public void onStart(){Bundle result=new Bundle();String name="live-provider-"+UUID.randomUUID()+".db";Store store=new Store(getTargetContext(),name);File scratch=new File(getTargetContext().getCacheDir(),"asr-part-live.wav");try{
  MimoClient chat=new MimoClient(getTargetContext(),store,"chat");JSONArray ids=chat.models();if(ids.length()<1)throw new IOException("empty models");
  String reply=chat.chat("请仅回答：连接成功",false);if(reply.isEmpty())throw new IOException("empty reply");MimoClient speech=new MimoClient(getTargetContext(),store,"asr");String text=speech.transcribe(new File("/data/local/tmp/xigua-asr-acceptance.wav"),scratch,value->{});
  IntentRouter route=IntentRouter.classify(text);if(!"feeding".equals(route.kind)||CareRules.voiceQuantity(text)!=150)throw new IOException("synthetic phrase did not route to expected feeding amount");
  result.putString("stream","PASS live Android provider: models="+ids.length()+", chat response present, synthetic ASR → 150 mL feeding confirmation; no record saved\n");finish(Activity.RESULT_OK,result);
 }catch(Throwable e){result.putString("stream","FAIL live Android provider: "+e.getClass().getSimpleName()+" / "+e.getMessage()+"\n");finish(Activity.RESULT_CANCELED,result);}finally{scratch.delete();store.close();getTargetContext().deleteDatabase(name);}}
}
