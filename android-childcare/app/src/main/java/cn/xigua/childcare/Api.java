package cn.xigua.childcare;

import android.content.ContentValues;
import android.database.sqlite.SQLiteDatabase;
import org.json.*;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.time.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicBoolean;

/** Personal API v1 adapter. The production endpoint is never assumed available. */
final class Api {
    interface Transport {JSONObject call(String method,String path,JSONObject body)throws IOException,JSONException;
        default JSONObject contextual(String method,String path,JSONObject body,String instance)throws IOException,JSONException{return call(method,path,body);}}
    private Transport transport;
    private final CareApp app;private final Store store;private final AtomicBoolean cancelled;private final String base,token;
    Api(CareApp app){this(app,new AtomicBoolean());}
    Api(CareApp app,AtomicBoolean cancelled){this.app=app;store=app.store;this.cancelled=cancelled;synchronized(store){base=store.get("base_url","");token=store.get("token","");}}
    Api(CareApp app,Store isolated,Transport transport){this.app=app;store=isolated;cancelled=new AtomicBoolean();synchronized(store){base=store.get("base_url","");token=store.get("token","");}this.transport=transport;}
    JSONObject http(String method,String path,JSONObject body)throws IOException,JSONException{
        return httpContext(method,path,body,store.get("server_instance_id",""));
    }
    private JSONObject httpContext(String method,String path,JSONObject body,String instance)throws IOException,JSONException{
        if(transport!=null)return transport.contextual(method,path,body,instance);
        if(base.isEmpty()||token.isEmpty())throw new IOException("尚未配置云端连接");if(cancelled.get())throw new IOException("已停止");
        URL url=new URL(base+path);if(!"https".equals(url.getProtocol()))throw new IOException("需要 HTTPS");HttpURLConnection c=(HttpURLConnection)url.openConnection();c.setInstanceFollowRedirects(false);c.setConnectTimeout(10000);c.setReadTimeout(30000);c.setRequestMethod(method);c.setRequestProperty("Authorization","Bearer "+token);c.setRequestProperty("Accept","application/json");if(!instance.isEmpty())c.setRequestProperty("X-Personal-Instance-ID",instance);
        try{if(body!=null){byte[] data=body.toString().getBytes(StandardCharsets.UTF_8);if(data.length>1024*1024)throw new IOException("请求超过大小上限");c.setDoOutput(true);c.setRequestProperty("Content-Type","application/json");c.setFixedLengthStreamingMode(data.length);try(OutputStream out=c.getOutputStream()){out.write(data);}}
            int status=c.getResponseCode();if(status<200||status>=300){String reason=status==401||status==403?"凭据或权限不匹配":status==404?"服务器尚未提供此接口":status==503?"服务或 AI 提供方尚未开启":"服务返回 "+status;throw new IOException(reason);}
            if(c.getContentLengthLong()>1024*1024)throw new IOException("响应超过 1 MiB 上限");try(InputStream in=c.getInputStream();ByteArrayOutputStream out=new ByteArrayOutputStream()){byte[] buffer=new byte[8192];int n,total=0;while((n=in.read(buffer))!=-1){if(cancelled.get())throw new IOException("已停止");total+=n;if(total>1024*1024)throw new IOException("响应超过 1 MiB 上限");out.write(buffer,0,n);}return new JSONObject(out.toString("UTF-8"));}
        }finally{c.disconnect();}
    }
    private JSONObject capabilities()throws Exception{
        JSONObject cap=httpContext("GET","/capabilities",null,"");if(!"personal-api-v1".equals(cap.optString("protocol")))throw new IOException("服务器协议不兼容");
        JSONObject baby=null;JSONArray profiles=cap.optJSONArray("profiles");boolean allowed=false;
        if(profiles!=null)for(int k=0;k<profiles.length();k++){JSONObject p=profiles.optJSONObject(k);if(p!=null&&"baby".equals(p.optString("id"))){baby=p;allowed=true;}else if("baby".equals(profiles.optString(k)))allowed=true;}
        if(!allowed)throw new IOException("当前凭据不允许访问孩子档案");
        if(cap.optBoolean("instance_context_required")){
            String server=cap.optString("server_instance_id"),profile=baby==null?"":baby.optString("instance_id");
            try{if(!UUID.fromString(server).toString().equals(server)||!UUID.fromString(profile).toString().equals(profile))throw new IllegalArgumentException();}catch(Exception e){throw new IOException("服务器身份信息无效");}
            synchronized(store){String old=store.get("server_instance_id","");
                if(!old.isEmpty()&&(!old.equals(server)||!store.get("profile_instance_id","").equals(profile)||!store.get("instance_base",base).equals(base))){
                    store.getWritableDatabase().execSQL("UPDATE outbox SET status='conflict',error='instance_context_mismatch' WHERE status IN ('pending','deferred')");throw new IOException("备份服务身份已改变，本地记录已保留；请先导出再重新连接");
                }
                if(old.isEmpty()){store.set("server_instance_id",server);store.set("profile_instance_id",profile);store.set("instance_base",base);}
                store.set("instance_context_required","1");
                for(JSONObject op:store.query("SELECT seq,body FROM outbox WHERE status IN ('pending','deferred')")){
                    JSONObject c=Store.json(op.optString("body")).optJSONObject("_sync_context");
                    if(c==null||c.optString("server").isEmpty()||c.optString("profile").isEmpty())store.fail(op.optLong("seq"),"instance_context_missing",null);
                    else if(!server.equals(c.optString("server"))||!profile.equals(c.optString("profile"))||!base.equals(c.optString("base")))store.fail(op.optLong("seq"),"instance_context_mismatch",null);
                }
            }
        }else if(!store.get("server_instance_id","").isEmpty())throw new IOException("备份服务身份校验不可用，本地记录已保留");
        store.set("capabilities",cap.toString());return cap;
    }
    boolean sync(){if(base.isEmpty()){store.set("sync_status","仅本地 · 尚未配置云端");app.changed();return true;}if(token.isEmpty()){store.set("sync_status","家庭服务器已内置，等待育儿凭据");app.changed();return false;}try{store.set("sync_status","正在同步");app.changed();JSONObject cap=capabilities();if(cap.optBoolean("family_profile"))syncFamilyProfile();JSONArray profiles=http("GET","/profiles",null).getJSONArray("items");long remoteGeneration=0;for(int k=0;k<profiles.length();k++)if("baby".equals(profiles.getJSONObject(k).optString("id")))remoteGeneration=profiles.getJSONObject(k).getLong("generation");if(remoteGeneration<1)throw new IOException("缺少孩子档案代次");
        synchronized(store){if("0".equals(store.get("generation","0"))){SQLiteDatabase db=store.getWritableDatabase();db.beginTransaction();try{store.set("generation",""+remoteGeneration);db.execSQL("UPDATE outbox SET generation=? WHERE generation=0",new Object[]{remoteGeneration});db.setTransactionSuccessful();}finally{db.endTransaction();}}}
        pull();int processed=0;while(!cancelled.get()&&processed++<100){List<JSONObject> pending=store.outbox();if(pending.isEmpty())break;JSONObject op=pending.get(0);JSONObject wire;
            synchronized(store){List<JSONObject> claim=store.query("SELECT * FROM outbox WHERE seq=? AND id=? AND status='pending'",String.valueOf(op.optLong("seq")),op.optString("id"));if(claim.isEmpty())continue;op=claim.get(0);if(!op.isNull("wire"))wire=Store.json(op.optString("wire"));else{JSONObject local=Store.json(op.optString("body")),current=store.event(op.optString("entity"));long rev=current==null?0:current.optLong("revision");
                if("delete".equals(op.optString("action"))&&rev==0){store.getWritableDatabase().delete("outbox","seq=?",new String[]{""+op.optLong("seq")});continue;}
                wire=Store.obj("operation_id",op.optString("id"),"action",op.optString("action"),"profile_id","baby","domain","childcare","entity_id",op.optString("entity"),"generation",op.optLong("generation"),"base_revision",rev==0?null:rev);
                if("1".equals(store.get("instance_context_required","")))wire.put("profile_instance_id",local.getJSONObject("_sync_context").getString("profile"));if(!"delete".equals(op.optString("action")))wire.put("record",record(local));if(!store.freeze(op.optLong("seq"),wire.toString()))continue;}}
            JSONObject context=Store.json(op.optString("body")).optJSONObject("_sync_context");JSONObject response=httpContext("POST","/sync/push",Store.obj("client_id",store.get("client_id",""),"operations",new JSONArray().put(wire)),context==null?"":context.optString("server"));JSONObject receipt=response.getJSONArray("receipts").getJSONObject(0);if(!op.optString("id").equals(receipt.optString("operation_id")))throw new IOException("操作回执身份不匹配");
            if("applied".equals(receipt.optString("status")))store.acknowledge(op,receipt.getLong("revision"),wire.optJSONObject("record")!=null&&!wire.getJSONObject("record").getJSONObject("data").has("caregiver")&&Store.json(store.get("capabilities","{}")).optBoolean("childcare_caregiver")&&!Store.json(op.optString("body")).optString("caregiver_name").isEmpty());else store.fail(op.optLong("seq"),receipt.optString("code","sync_conflict"),receipt.optJSONObject("record"));app.changed();
        }pull();store.set("sync_status",store.pending()==0?"已同步 · 修订 "+store.get("cursor","0"):store.pending()+" 项待处理（含进行中的睡眠）");if("1".equals(store.get("ai_via_backend","0")))processJobs();else processExistingJobs();app.changed();return true;
    }catch(Exception e){store.set("sync_status",e.getMessage()==null?"同步未完成，稍后重试":e.getMessage());app.changed();return false;}}
    private void syncFamilyProfile()throws Exception{JSONObject profile=http("GET","/family/profile",null);String name=profile.optString("name").trim(),birthday=profile.optString("birthday").trim(),tz=profile.optString("timezone","Asia/Shanghai").trim();if(name.isEmpty())throw new IOException("服务器宝宝档案缺少昵称");if(!birthday.isEmpty())try{java.time.LocalDate.parse(birthday);}catch(Exception e){throw new IOException("服务器宝宝生日格式无效");}synchronized(store){String localRevision=store.get("family_profile_revision","");String revision=profile.optString("revision",profile.optString("updated_at",""));if(localRevision.isEmpty()||revision.equals(localRevision)||profile.optLong("updated_at",0)>=Long.parseLong(store.get("child_updated","0"))){store.set("child_name",name);store.set("child_birthday",birthday);if(!tz.isEmpty())store.set("timezone",tz);store.set("child_registered","1");store.set("family_profile_revision",revision);store.set("sync_profile_status","已同步宝宝档案");}}}
    private JSONObject record(JSONObject e)throws JSONException,IOException{
        String kind=e.optString("kind");JSONObject previous=e.optString("remote_json").isEmpty()?null:Store.json(e.optString("remote_json"));
        JSONObject original=previous==null?null:local(previous),data=previous==null?Store.obj("event_type",kind):Store.json(previous.getJSONObject("data").toString());
        data.put("event_type",kind);
        if(original==null||!e.optString("note").equals(original.optString("note")))data.put("note",e.optString("note").isEmpty()?JSONObject.NULL:e.optString("note"));
        boolean attribution=Store.json(store.get("capabilities","{}")).optBoolean("childcare_caregiver");String name=e.optString("caregiver_name");
        if(!name.isEmpty()&&!attribution)throw new IOException("备份服务尚不支持照护者归属，署名记录已留在手机，请更新服务后重试");
        if(attribution)data.put("caregiver",name.isEmpty()?JSONObject.NULL:Store.obj("id",e.optString("caregiver_id").isEmpty()?null:e.optString("caregiver_id"),"name",name));
        if("feeding".equals(kind)){
            if(original==null||e.optDouble("amount")!=original.optDouble("amount"))data.put("amount_ml",e.optDouble("amount",0)==0?JSONObject.NULL:e.optDouble("amount",0));
            if(original==null||!e.optString("detail").equals(original.optString("detail")))data.put("ingredient",e.optString("detail").isEmpty()?JSONObject.NULL:e.optString("detail"));
        }
        if("diaper".equals(kind)&&(original==null||!e.optString("detail").equals(original.optString("detail"))))data.put("diaper_kind","大便".equals(e.optString("detail"))?"dirty":"尿便都有".equals(e.optString("detail"))?"mixed":"尿湿".equals(e.optString("detail"))?"wet":JSONObject.NULL);
        boolean sameTimes=original!=null&&e.optLong("start")==original.optLong("start")&&e.optLong("end")==original.optLong("end");
        if(!sameTimes){if(e.optLong("end")>0){data.put("start_at",iso(e.optLong("start")));data.put("end_at",iso(e.optLong("end")));data.put("duration_seconds",(e.optLong("end")-e.optLong("start"))/1000.0);}else{data.put("start_at",JSONObject.NULL);data.put("end_at",JSONObject.NULL);data.put("duration_seconds",JSONObject.NULL);}}
        JSONObject result=Store.obj("occurred_at",sameTimes?previous.opt("occurred_at"):e.optLong("start")==0?JSONObject.NULL:iso(e.optLong("start")),"recorded_at",previous==null?e.optString("recorded_at",Instant.now().toString()):previous.opt("recorded_at"),"timezone",e.optString("timezone","Asia/Shanghai"),"source",previous==null?Store.obj("provider","manual","source_id",null):previous.get("source"),"data",data,"media_ids",previous==null?new JSONArray():previous.get("media_ids"));return result;
    }
    private static String iso(long ms){return Instant.ofEpochMilli(ms).toString();}
    private static long epoch(String text){return text==null||text.isEmpty()||"null".equals(text)?0:OffsetDateTime.parse(text).toInstant().toEpochMilli();}
    private JSONObject local(JSONObject r)throws JSONException{JSONObject data=r.getJSONObject("data");String kind=data.getString("event_type"),detail=data.isNull("ingredient")?"":data.optString("ingredient","");if("diaper".equals(kind))detail="dirty".equals(data.optString("diaper_kind"))?"大便":"mixed".equals(data.optString("diaper_kind"))?"尿便都有":"wet".equals(data.optString("diaper_kind"))?"尿湿":"";long start=epoch(r.optString("occurred_at"));if(data.has("start_at")&&!data.isNull("start_at"))start=epoch(data.optString("start_at"));
        JSONObject ours=store.event(r.optString("id")),actor=data.optJSONObject("caregiver");String actorId=data.has("caregiver")?(actor==null||actor.isNull("id")?"":actor.optString("id")):ours==null?"":ours.optString("caregiver_id"),actorName=data.has("caregiver")?(actor==null?"":actor.optString("name")):ours==null?"":ours.optString("caregiver_name");return Store.obj("caregiver_id",actorId,"caregiver_name",actorName,"id",r.getString("id"),"kind",kind,"start",start,"end",epoch(data.optString("end_at")),"active",0,"duration",(long)(data.optDouble("duration_seconds",0)*1000),"remote_json",r.toString(),"amount",data.optDouble("amount_ml",0),"detail",detail,"note",data.isNull("note")?"":data.optString("note", ""),"timezone",r.optString("timezone","Asia/Shanghai"),"revision",r.optLong("revision"),"deleted",r.optBoolean("deleted")?1:0);
    }
    private void pull()throws Exception{long cursor=Long.parseLong(store.get("cursor","0")),through=-1;for(int page=0;page<100;page++){
        JSONObject result=http("GET","/sync/pull?profile_id=baby&after="+cursor+"&limit=100"+(through<0?"":"&through="+through),null);if(through<0)through=result.getLong("through");if(result.getLong("through")!=through)throw new IOException("拉取修订窗口改变");JSONArray changes=result.getJSONArray("changes");if(changes.length()>100)throw new IOException("拉取页超出上限");long next=result.getLong("next_cursor");if(next<cursor||next>through)throw new IOException("拉取游标无效");
        synchronized(store){SQLiteDatabase db=store.getWritableDatabase();db.beginTransaction();try{for(int k=0;k<changes.length();k++){JSONObject change=changes.getJSONObject(k);if("clear".equals(change.optString("kind"))){long gen=change.getLong("generation");db.execSQL("UPDATE outbox SET status='conflict',error='generation_mismatch' WHERE generation<?",new Object[]{gen});db.execSQL("DELETE FROM events WHERE revision>0 AND id NOT IN (SELECT entity FROM outbox)");if(gen>Long.parseLong(store.get("generation","0")))store.set("generation",""+gen);continue;}
                JSONObject remote=change.optJSONObject("record");if(remote==null||!"childcare".equals(remote.optString("domain")))continue;String id=remote.getString("id");boolean hasPending=!store.query("SELECT seq FROM outbox WHERE entity=? LIMIT 1",id).isEmpty();if(!hasPending){db.insertWithOnConflict("events",null,Store.eventValues(local(remote)),SQLiteDatabase.CONFLICT_REPLACE);store.set("event_context_"+id,store.syncContext().toString());}
            }store.set("cursor",""+next);db.setTransactionSuccessful();}finally{db.endTransaction();}}
        cursor=next;if(!result.getBoolean("has_more"))return;if(next==0)throw new IOException("拉取没有前进");}throw new IOException("同步页数达到上限，请再次同步");}
    JSONObject statistics(LocalDate day,ZoneId zone)throws Exception{return http("GET","/statistics?profile_id=baby&start="+day+"&end="+day+"&timezone="+URLEncoder.encode(zone.getId(),"UTF-8")+"&at_revision="+store.get("cursor","0"),null);}
    void resolve(JSONObject op,boolean keepLocal)throws Exception{synchronized(store){if(keepLocal&&Arrays.asList("generation_mismatch","instance_context_mismatch").contains(op.optString("error")))throw new IllegalArgumentException("云端已清空此代次，旧记录保留待处理；不能自动重新上传");JSONObject remote=op.isNull("remote")?null:Store.json(op.optString("remote"));SQLiteDatabase db=store.getWritableDatabase();db.beginTransaction();try{String id=op.optString("entity");JSONObject ours=store.event(id);db.delete("outbox","entity=?",new String[]{id});
        if(remote!=null)db.insertWithOnConflict("events",null,Store.eventValues(local(remote)),SQLiteDatabase.CONFLICT_REPLACE);else db.delete("events","id=?",new String[]{id});
        if(keepLocal&&ours!=null){ours.put("_caregiver_changed",true);if("instance_context_missing".equals(op.optString("error")))ours.put("_rebind_context",true);if(remote!=null&&remote.optBoolean("deleted")){ours.put("id",UUID.randomUUID().toString());ours.put("revision",0);store.save(ours);}else if(ours.optInt("deleted")==1){if(remote!=null)store.delete(id);}else store.save(ours);}db.setTransactionSuccessful();}finally{db.endTransaction();}}app.changed();}
    private static final class IdentityMismatch extends IOException{IdentityMismatch(){super("AI 回复身份不匹配，请重新发起");}}
    void processJobs(){processJobs(false);}
    void processExistingJobs(){processJobs(true);}
    private void processJobs(boolean onlyExisting){try{JSONObject cap=capabilities();JSONObject ai=cap.optJSONObject("ai");List<JSONObject> rows=store.query("SELECT * FROM requests WHERE status IN ('queued','pending','running')"+(onlyExisting?" AND job<>''":"")+" ORDER BY created LIMIT 8");for(JSONObject r:rows){if(cancelled.get())return;String id=r.optString("id"),job=r.optString("job");if("direct".equals(store.get("request_transport_"+id,"")))continue;try{
        if(ai==null||!ai.optBoolean("enabled")){store.requestState(id,"failed",null,null,"后端 AI 提供方尚未开启；可稍后重试");continue;}JSONObject result;JSONObject ctx=Store.json(store.get("request_context_"+id,"{}"));if("1".equals(store.get("instance_context_required",""))&&(!ctx.optString("server").equals(store.get("server_instance_id",""))||!ctx.optString("profile").equals(store.get("profile_instance_id",""))||!ctx.optString("base").equals(base))){store.requestState(id,"failed",null,null,"服务身份已改变，请重新发起");continue;}if(job.isEmpty()){result=http("POST","/ai/jobs",Store.obj("request_id",id,"profile_id","baby","request_type","story".equals(r.optString("mode"))?"story":"question","prompt",r.optString("prompt")));}else result=http("GET","/ai/jobs/"+encode(job),null);
        String expected="story".equals(r.optString("mode"))?"story":"question";if(!id.equals(result.optString("request_id"))||!expected.equals(result.optString("request_type"))||!"baby".equals(result.optString("profile_id")))throw new IdentityMismatch();String returnedJob=result.optString("id");if(returnedJob.isEmpty()||(!job.isEmpty()&&!job.equals(returnedJob)))throw new IdentityMismatch();if(job.isEmpty()){job=returnedJob;store.requestState(id,"pending",job,null,"");}
        JSONObject current=store.requestById(id);if(current==null||"cancelled".equals(current.optString("status"))){cancelJob(job);continue;}String state=result.optString("state");String text=result.isNull("result")?"":result.optString("result");if(text.length()>12000)throw new IOException("回复超过 12000 字显示上限");store.requestState(id,"queued".equals(state)?"pending":state,job,text,result.optString("error_code",""));
    }catch(Exception e){store.requestState(id,e instanceof IdentityMismatch?"failed":"pending",e instanceof IdentityMismatch?"":job,null,e.getMessage()==null?"等待联网重试":e.getMessage());}}app.changed();}catch(Exception e){for(JSONObject r:store.query("SELECT id FROM requests WHERE status IN ('queued','pending','running')"+(onlyExisting?" AND job<>''":"")+" LIMIT 8"))if(!"direct".equals(store.get("request_transport_"+r.optString("id"),"")))store.requestState(r.optString("id"),"pending",null,null,e.getMessage()==null?"等待连接后重试":e.getMessage());app.changed();}}
    void cancelJob(String job){try{http("POST","/ai/jobs/"+encode(job)+"/cancel",Store.obj());}catch(Exception ignored){}}
    void catalog(String category){catalogPage(category,0);}
    void catalogMore(String category){String offset=store.get("catalog_next_"+category,"");if(!offset.isEmpty())catalogPage(category,Integer.parseInt(offset));}
    private void catalogPage(String category,int offset){try{
        JSONObject cap=capabilities();JSONArray categories=cap.getJSONArray("audio_categories");boolean enabled=false;for(int k=0;k<categories.length();k++)enabled|=category.equals(categories.optString(k));if(!enabled)throw new IOException("服务器不支持此音频分类");
        JSONObject response=http("GET","/audio/catalog?category="+encode(category)+"&limit=100&offset="+offset+"&client=android",null);JSONArray rows=response.getJSONArray("items");if(rows.length()>100||response.optInt("offset",offset)!=offset)throw new IOException("音频目录页无效");int next=response.isNull("next_offset")?-1:response.getInt("next_offset");if(next!=-1&&next<=offset)throw new IOException("音频目录没有前进");
        Set<String> formats=new HashSet<>();JSONArray allowed=cap.getJSONObject("audio_formats").getJSONArray("android");for(int k=0;k<allowed.length();k++)formats.add(allowed.getString(k));
        synchronized(store){SQLiteDatabase db=store.getWritableDatabase();db.beginTransaction();try{if(offset==0)db.delete("tracks","category=? AND local=0",new String[]{category});for(int k=0;k<rows.length();k++){JSONObject row=rows.getJSONObject(k);if(!formats.contains(row.optString("mime_type")))continue;String url=row.optString("play_url");if(!url.startsWith("https://"))url=new URI(base+"/").resolve(url).toString();if(!url.startsWith("https://"))continue;store.track(row.getString("id"),row.getString("title"),category,url,false);}store.set("catalog_next_"+category,next<0?"":String.valueOf(next));db.setTransactionSuccessful();}finally{db.endTransaction();}}app.changed();
    }catch(Exception e){store.set("sync_status",e.getMessage()==null?"此分类暂时不可用":e.getMessage());app.changed();}}
    private static String encode(String s)throws UnsupportedEncodingException{return URLEncoder.encode(s,"UTF-8");}
}
