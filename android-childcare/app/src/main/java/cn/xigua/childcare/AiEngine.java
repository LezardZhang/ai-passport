package cn.xigua.childcare;
import org.json.JSONObject;
import java.util.concurrent.ConcurrentHashMap;

/** Durable request ownership shared by the single Care view. */
final class AiEngine {
    private static final ConcurrentHashMap<String,MimoClient> active=new ConcurrentHashMap<>();
    static JSONObject frozenProvider(Store store,String id)throws java.io.IOException{String frozen=store.get("request_provider_"+id,"");if(!"direct".equals(store.get("request_transport_"+id,""))||frozen.isEmpty())throw new java.io.IOException("旧请求缺少原服务配置，请确认后重新发起");return Store.json(frozen);}
    static void cancel(String id){MimoClient c=active.get(id);if(c!=null)c.cancel();}
    static void process(CareApp app){Store store=app.store;if("1".equals(store.get("ai_via_backend","0"))){new Api(app).processJobs();}if(!store.get("base_url","").isEmpty()&&!store.query("SELECT id FROM requests WHERE job<>'' AND status IN ('pending','running') LIMIT 1").isEmpty())new Api(app).processExistingJobs();
        for(JSONObject request:store.query("SELECT * FROM requests WHERE status='queued' AND job='' ORDER BY created,id LIMIT 8")){String id=request.optString("id");if("backend".equals(store.get("request_transport_"+id,"")))continue;MimoClient client=null;try{
            if(!store.claimRequest(id))continue;app.changed();client=new MimoClient(frozenProvider(store,id));active.put(id,client);JSONObject current=store.requestById(id);if(current==null||"cancelled".equals(current.optString("status"))){client.cancel();continue;}
            String text=client.chat(request.optString("prompt"),"story".equals(request.optString("mode")));store.requestState(id,"completed",null,text,"");
        }catch(Exception e){store.requestState(id,"failed",null,null,e instanceof java.io.IOException?e.getMessage():"生成暂时不可用，请重试");}finally{active.remove(id);JSONObject done=store.requestById(id);if(done!=null&&java.util.Arrays.asList("completed","failed","cancelled").contains(done.optString("status")))store.set("request_provider_"+id,"");app.changed();}}
    }
}
