package cn.xigua.childcare;

import android.content.*;
import android.database.Cursor;
import android.database.sqlite.*;
import org.json.*;
import java.util.*;
import java.time.*;
import java.io.*;

/** Durable local source of truth. All multi-row writes are transactions. */
public final class Store extends SQLiteOpenHelper {
    public Store(Context c){this(c,"childcare.db");}
    public Store(Context c,String name){super(c,name,null,3);setWriteAheadLoggingEnabled(true);}
    @Override public void onCreate(SQLiteDatabase d){
        d.execSQL("CREATE TABLE events(id TEXT PRIMARY KEY,kind TEXT NOT NULL,start INTEGER NOT NULL,end INTEGER NOT NULL DEFAULT 0,amount INTEGER NOT NULL DEFAULT 0,detail TEXT NOT NULL DEFAULT '',note TEXT NOT NULL DEFAULT '',timezone TEXT NOT NULL,revision INTEGER NOT NULL DEFAULT 0,deleted INTEGER NOT NULL DEFAULT 0,active INTEGER NOT NULL DEFAULT 0,duration INTEGER NOT NULL DEFAULT 0,remote_json TEXT NOT NULL DEFAULT '',caregiver_id TEXT NOT NULL DEFAULT '',caregiver_name TEXT NOT NULL DEFAULT '')");
        d.execSQL("CREATE INDEX event_time ON events(start)");
        d.execSQL("CREATE UNIQUE INDEX single_active_sleep ON events(kind) WHERE kind='sleep' AND active=1 AND deleted=0");
        d.execSQL("CREATE TABLE outbox(seq INTEGER PRIMARY KEY AUTOINCREMENT,id TEXT UNIQUE NOT NULL,entity TEXT NOT NULL,action TEXT NOT NULL,body TEXT NOT NULL,generation INTEGER NOT NULL DEFAULT 0,wire TEXT,status TEXT NOT NULL DEFAULT 'pending',error TEXT NOT NULL DEFAULT '',remote TEXT)");
        d.execSQL("CREATE INDEX outbox_entity ON outbox(entity)");
        d.execSQL("CREATE TABLE meta(key TEXT PRIMARY KEY,value TEXT NOT NULL)");
        d.execSQL("CREATE TABLE reminders(id TEXT PRIMARY KEY,title TEXT NOT NULL,due INTEGER NOT NULL,done INTEGER NOT NULL DEFAULT 0)");
        d.execSQL("CREATE TABLE requests(id TEXT PRIMARY KEY,mode TEXT NOT NULL,prompt TEXT NOT NULL,job TEXT NOT NULL DEFAULT '',status TEXT NOT NULL,response TEXT NOT NULL DEFAULT '',error TEXT NOT NULL DEFAULT '',created INTEGER NOT NULL)");
        d.execSQL("CREATE TABLE tracks(id TEXT PRIMARY KEY,title TEXT NOT NULL,category TEXT NOT NULL,url TEXT NOT NULL,local INTEGER NOT NULL DEFAULT 0)");
        createMembers(d);
        meta(d,"client_id","childcare-"+UUID.randomUUID());meta(d,"child_name","西瓜");meta(d,"timezone","Asia/Shanghai");meta(d,"cursor","0");meta(d,"generation","0");
    }
    private static void createMembers(SQLiteDatabase db){db.execSQL("CREATE TABLE members(id TEXT PRIMARY KEY,name TEXT NOT NULL,relation TEXT NOT NULL,phone TEXT NOT NULL DEFAULT '',active INTEGER NOT NULL DEFAULT 1,created INTEGER NOT NULL,updated INTEGER NOT NULL)");}
    @Override public void onUpgrade(SQLiteDatabase db,int old,int next){
        if(old<2){db.execSQL("ALTER TABLE events ADD COLUMN active INTEGER NOT NULL DEFAULT 0");db.execSQL("ALTER TABLE events ADD COLUMN duration INTEGER NOT NULL DEFAULT 0");db.execSQL("ALTER TABLE events ADD COLUMN remote_json TEXT NOT NULL DEFAULT ''");db.execSQL("DROP INDEX single_active_sleep");db.execSQL("UPDATE events SET active=1 WHERE kind='sleep' AND end=0 AND start>0 AND revision=0 AND deleted=0");db.execSQL("CREATE UNIQUE INDEX single_active_sleep ON events(kind) WHERE kind='sleep' AND active=1 AND deleted=0");}
        if(old<3){createMembers(db);db.execSQL("ALTER TABLE events ADD COLUMN caregiver_id TEXT NOT NULL DEFAULT ''");db.execSQL("ALTER TABLE events ADD COLUMN caregiver_name TEXT NOT NULL DEFAULT ''");}
    }
    public synchronized List<JSONObject> members(){return query("SELECT * FROM members ORDER BY active DESC,created,id LIMIT 100");}
    public synchronized JSONObject member(String id){List<JSONObject> rows=query("SELECT * FROM members WHERE id=?",id);return rows.isEmpty()?null:rows.get(0);}
    public synchronized JSONObject selectedMember(){JSONObject m=member(get("selected_member",""));return m!=null&&m.optInt("active")==1?m:null;}
    public synchronized String registerMember(String name,String relation,String phone){FamilyRules.validateMember(name,relation,phone);if(members().size()>=100)throw new IllegalArgumentException("本机人物上限100位，请先整理档案");String id=UUID.randomUUID().toString();writeMember(id,name,relation,phone,true);return id;}
    public synchronized void updateMember(String id,String name,String relation,String phone){FamilyRules.validateMember(name,relation,phone);if(member(id)==null)throw new IllegalArgumentException("人物不存在，请重新打开档案");writeMember(id,name,relation,phone,false);}
    private void writeMember(String id,String name,String relation,String phone,boolean create){
        if(!query("SELECT id FROM members WHERE name=? AND id!=?",name.trim(),id).isEmpty())throw new IllegalArgumentException("已有这个称谓，请直接切换使用");
        SQLiteDatabase db=getWritableDatabase();db.beginTransaction();try{ContentValues v=new ContentValues();v.put("name",name.trim());v.put("relation",relation.trim());v.put("phone",phone.trim());v.put("updated",System.currentTimeMillis());if(create){v.put("id",id);v.put("created",System.currentTimeMillis());v.put("active",1);db.insertOrThrow("members",null,v);if(selectedMember()==null)meta(db,"selected_member",id);}else db.update("members",v,"id=?",new String[]{id});db.setTransactionSuccessful();}finally{db.endTransaction();}
    }
    public synchronized void selectMember(String id){JSONObject m=member(id);if(m==null||m.optInt("active")!=1)throw new IllegalArgumentException("请先恢复这位照护者");set("selected_member",id);}
    public synchronized void setMemberActive(String id,boolean active){if(member(id)==null)throw new IllegalArgumentException("人物不存在");if(!active&&id.equals(get("selected_member","")))throw new IllegalArgumentException("请先切换到另一位照护者，再停用当前人物");ContentValues v=new ContentValues();v.put("active",active?1:0);v.put("updated",System.currentTimeMillis());getWritableDatabase().update("members",v,"id=?",new String[]{id});}
    public synchronized void saveChild(String name,String birthday,String timezone){FamilyRules.validateChild(name,birthday,timezone,LocalDate.now(ZoneId.of(get("timezone","Asia/Shanghai"))));SQLiteDatabase db=getWritableDatabase();db.beginTransaction();try{meta(db,"child_name",name.trim());meta(db,"child_birthday",birthday.trim());meta(db,"timezone",timezone.trim());meta(db,"child_registered","1");meta(db,"child_updated",String.valueOf(System.currentTimeMillis()));db.setTransactionSuccessful();}finally{db.endTransaction();}}
    public static JSONObject json(String s){try{return new JSONObject(s);}catch(JSONException e){throw new IllegalArgumentException(e);}}
    public static JSONObject obj(Object... values){JSONObject o=new JSONObject();try{for(int i=0;i<values.length;i+=2)o.put((String)values[i],values[i+1]==null?JSONObject.NULL:values[i+1]);}catch(JSONException e){throw new IllegalArgumentException(e);}return o;}
    private static void meta(SQLiteDatabase d,String key,String value){ContentValues v=new ContentValues();v.put("key",key);v.put("value",value);d.insertWithOnConflict("meta",null,v,SQLiteDatabase.CONFLICT_REPLACE);}
    public synchronized void set(String key,String value){meta(getWritableDatabase(),key,value);}
    synchronized void configureBackup(String base,String token){SQLiteDatabase db=getWritableDatabase();db.beginTransaction();try{meta(db,"base_url",base);meta(db,"token",token);db.setTransactionSuccessful();}finally{db.endTransaction();}}
    public synchronized String get(String key,String fallback){try(Cursor c=getReadableDatabase().rawQuery("SELECT value FROM meta WHERE key=?",new String[]{key})){return c.moveToFirst()?c.getString(0):fallback;}}
    private static JSONObject row(Cursor c){JSONObject o=new JSONObject();try{for(int i=0;i<c.getColumnCount();i++){if(c.isNull(i))o.put(c.getColumnName(i),JSONObject.NULL);else if(c.getType(i)==Cursor.FIELD_TYPE_INTEGER)o.put(c.getColumnName(i),c.getLong(i));else if(c.getType(i)==Cursor.FIELD_TYPE_FLOAT)o.put(c.getColumnName(i),c.getDouble(i));else o.put(c.getColumnName(i),c.getString(i));}}catch(JSONException e){throw new IllegalArgumentException(e);}return o;}
    public synchronized List<JSONObject> query(String sql,String... args){List<JSONObject> rows=new ArrayList<>();try(Cursor c=getReadableDatabase().rawQuery(sql,args)){while(c.moveToNext())rows.add(row(c));}return rows;}
    public synchronized JSONObject event(String id){List<JSONObject> r=query("SELECT * FROM events WHERE id=?",id);if(r.isEmpty())return null;JSONObject e=r.get(0);String saved=get("event_context_"+id,"");try{e.put("_sync_context",saved.isEmpty()?obj("base","","server","","profile",""):json(saved));}catch(JSONException ex){throw new IllegalArgumentException(ex);}return e;}
    static ContentValues eventValues(JSONObject o){ContentValues v=new ContentValues();for(String k:new String[]{"id","kind","detail","note","timezone","remote_json","caregiver_id","caregiver_name"})v.put(k,o.optString(k));for(String k:new String[]{"start","end","revision","deleted","active","duration"})v.put(k,o.optLong(k));v.put("amount",o.optDouble("amount",0));return v;}
    synchronized JSONObject syncContext(){return obj("base",get("base_url",""),"server",get("server_instance_id",""),"profile",get("profile_instance_id",""));}
    synchronized void captureContext(JSONObject e){if(!e.has("_sync_context"))try{e.put("_sync_context",syncContext());}catch(JSONException ex){throw new IllegalArgumentException(ex);}}
    private void queue(SQLiteDatabase d,JSONObject e,String action){captureContext(e);meta(d,"event_context_"+e.optString("id"),e.optJSONObject("_sync_context").toString());ContentValues v=new ContentValues();v.put("id",UUID.randomUUID().toString());v.put("entity",e.optString("id"));v.put("action",action);v.put("body",e.toString());v.put("generation",Long.parseLong(get("generation","0")));if(e.optInt("active")==1&&"upsert".equals(action))v.put("status","deferred");d.insertOrThrow("outbox",null,v);}
    public synchronized String save(JSONObject e){
        SQLiteDatabase d=getWritableDatabase();d.beginTransaction();try{
            String id=e.optString("id");if(id.isEmpty()){id=UUID.randomUUID().toString();e.put("id",id);}
            JSONObject previous=event(id);
            boolean unknownHistory=previous!=null&&previous.optLong("start")==0&&e.optLong("start")==0;
            CareRules.validateAt(e.optString("kind"),e.optDouble("amount",0),unknownHistory?1:e.optLong("start"),e.optLong("end"),e.optString("note"),System.currentTimeMillis());
            boolean sameTimes=previous!=null&&e.optLong("start")==previous.optLong("start")&&e.optLong("end")==previous.optLong("end");
            boolean historicalSleep=previous!=null&&"sleep".equals(previous.optString("kind"))&&previous.optInt("active")==0&&previous.optLong("end")==0&&!previous.optString("remote_json").isEmpty();
            if(previous!=null&&!e.optBoolean("_rebind_context")){JSONObject original=previous.optJSONObject("_sync_context");JSONObject supplied=e.optJSONObject("_sync_context");if(supplied!=null&&!supplied.toString().equals(original.toString()))throw new IllegalArgumentException("记录的备份身份在编辑期间已变化，请重新打开确认");e.put("_sync_context",original);}else if(e.optBoolean("_rebind_context"))e.put("_sync_context",syncContext());e.remove("_rebind_context");e.put("timezone",e.optString("timezone",get("timezone","Asia/Shanghai")));e.put("revision",previous==null?0:previous.optLong("revision"));e.put("deleted",0);
            if(e.has("_expected_revision")&&previous!=null&&e.optLong("_expected_revision")!=previous.optLong("revision"))throw new IllegalArgumentException("记录在编辑期间已改变，请重新打开详情确认");
            boolean running="sleep".equals(e.optString("kind"))&&e.optLong("end")==0&&!historicalSleep;JSONObject other=activeSleep();if(running&&other!=null&&!id.equals(other.optString("id")))throw new IllegalArgumentException("已有进行中的睡眠，请先结束");
            e.put("active",running?1:0);e.put("duration",sameTimes?previous.optLong("duration"):e.optLong("end")>0?e.optLong("end")-e.optLong("start"):0);e.put("remote_json",previous==null?"":previous.optString("remote_json"));
            JSONObject caregiver=selectedMember();boolean changed=e.optBoolean("_caregiver_changed");
            if(previous!=null&&!changed){e.put("caregiver_id",previous.optString("caregiver_id"));e.put("caregiver_name",previous.optString("caregiver_name"));}
            else {if(!e.has("caregiver_id"))e.put("caregiver_id",caregiver==null?"":caregiver.optString("id"));if(!e.has("caregiver_name"))e.put("caregiver_name",caregiver==null?"":FamilyRules.caregiverTitle(caregiver.optString("name"),caregiver.optString("relation")));}
            if(e.optString("caregiver_name").length()>40||e.optString("caregiver_id").length()>128)throw new IllegalArgumentException("照护者资料超过长度限制");
            e.remove("_caregiver_changed");
            e.put("recorded_at",Instant.ofEpochMilli(System.currentTimeMillis()).toString());
            if(previous!=null&&previous.optInt("deleted")==1&&previous.optLong("revision")>0)throw new IllegalArgumentException("已同步删除的记录须通过冲突恢复流程处理");
            d.delete("outbox","entity=? AND status='deferred'",new String[]{id});d.insertWithOnConflict("events",null,eventValues(e),SQLiteDatabase.CONFLICT_REPLACE);queue(d,e,"upsert");d.setTransactionSuccessful();return id;
        }catch(JSONException ex){throw new IllegalArgumentException(ex);}finally{d.endTransaction();}
    }
    public synchronized JSONObject delete(String id){SQLiteDatabase d=getWritableDatabase();d.beginTransaction();try{JSONObject e=event(id);if(e==null)throw new IllegalArgumentException("记录不存在");JSONObject before=json(e.toString());e.put("deleted",1);d.delete("outbox","entity=? AND status='deferred'",new String[]{id});d.insertWithOnConflict("events",null,eventValues(e),SQLiteDatabase.CONFLICT_REPLACE);queue(d,e,"delete");d.setTransactionSuccessful();return before;}catch(JSONException ex){throw new IllegalArgumentException(ex);}finally{d.endTransaction();}}
    /** Undo is persisted as an intent; restore is explicit for acknowledged tombstones. */
    public synchronized void undo(JSONObject before){SQLiteDatabase d=getWritableDatabase();d.beginTransaction();try{String id=before.optString("id");JSONObject current=event(id);boolean frozenDelete=!query("SELECT seq FROM outbox WHERE entity=? AND action='delete' AND wire IS NOT NULL LIMIT 1",id).isEmpty();boolean queuedDelete=!query("SELECT seq FROM outbox WHERE entity=? AND action='delete' AND wire IS NULL LIMIT 1",id).isEmpty();
        if(queuedDelete&&!frozenDelete)d.delete("outbox","entity=? AND action='delete' AND wire IS NULL",new String[]{id});
        if(frozenDelete||(current!=null&&current.optInt("deleted")==1&&!queuedDelete)){id=UUID.randomUUID().toString();before.put("id",id);before.put("revision",0);}else before.put("revision",current==null?0:current.optLong("revision"));
        before.put("deleted",0);d.insertWithOnConflict("events",null,eventValues(before),SQLiteDatabase.CONFLICT_REPLACE);queue(d,before,"upsert");d.setTransactionSuccessful();}catch(JSONException e){throw new IllegalArgumentException(e);}finally{d.endTransaction();}}
    public synchronized JSONObject activeSleep(){List<JSONObject> r=query("SELECT * FROM events WHERE deleted=0 AND kind='sleep' AND active=1 LIMIT 1");return r.isEmpty()?null:r.get(0);}
    public synchronized void toggleSleep(long now){JSONObject e=activeSleep();if(e==null)e=obj("kind","sleep","start",now,"end",0,"amount",0,"detail","","note","");else try{if(now<=e.optLong("start"))throw new IllegalArgumentException("系统时间早于睡眠开始，请先校准时间或编辑开始时间");e.put("end",now);}catch(JSONException ex){throw new IllegalArgumentException(ex);}save(e);}
    public synchronized List<JSONObject> timeline(long from,long to){return timeline(from,to,0);}
    public synchronized List<JSONObject> timeline(long from,long to,int page){List<JSONObject> rows=query("SELECT events.*, (SELECT COUNT(*) FROM outbox WHERE entity=events.id) AS pending FROM events WHERE deleted=0 AND ((start>=? AND start<?) OR start=0 OR (kind='sleep' AND start<? AND (active=1 OR end>?))) ORDER BY (start=0),start ASC,id ASC LIMIT 100 OFFSET ?",String.valueOf(from),String.valueOf(to),String.valueOf(to),String.valueOf(from),String.valueOf(Math.max(0,page)*100L));for(JSONObject row:rows)try{row.put("_sync_context",event(row.optString("id")).optJSONObject("_sync_context"));}catch(JSONException ex){throw new IllegalArgumentException(ex);}return rows;}
    public synchronized JSONObject summary(long from,long to,long now){
        double milk=0;long feeds=0,known=0,diaper=0,sleep=0,activeMs=0;try(Cursor c=getReadableDatabase().rawQuery("SELECT kind,amount,start,end,active FROM events WHERE deleted=0 AND ((start>=? AND start<?) OR (kind='sleep' AND start<? AND (active=1 OR end>?)))",new String[]{String.valueOf(from),String.valueOf(to),String.valueOf(to),String.valueOf(from)})){
        while(c.moveToNext()){switch(c.getString(0)){case "feeding":if(c.getDouble(1)>0){milk+=c.getDouble(1);known++;}feeds++;break;case "diaper":diaper++;break;case "sleep":if(c.getInt(4)==1)activeMs+=CareRules.overlap(c.getLong(2),now,from,to);else if(c.getLong(3)>0)sleep+=CareRules.overlap(c.getLong(2),c.getLong(3),from,to);}}}
        return obj("milk",known==0?null:milk,"unknown_milk",feeds-known,"feeds",feeds,"diaper",diaper,"sleep_ms",sleep,"active_sleep_ms",activeMs,"pending",pending());
    }
    public synchronized int pending(){return (int)getReadableDatabase().compileStatement("SELECT COUNT(*) FROM outbox").simpleQueryForLong();}
    public synchronized List<JSONObject> outbox(){return query("SELECT * FROM outbox o WHERE status='pending' AND NOT EXISTS(SELECT 1 FROM outbox earlier WHERE earlier.entity=o.entity AND earlier.seq<o.seq AND earlier.status='conflict') ORDER BY seq LIMIT 1");}
    public synchronized boolean freeze(long seq,String wire){ContentValues v=new ContentValues();v.put("wire",wire);return getWritableDatabase().update("outbox",v,"seq=? AND status='pending' AND wire IS NULL",new String[]{""+seq})==1;}
    public synchronized void fail(long seq,String error,JSONObject remote){ContentValues v=new ContentValues();v.put("status","conflict");v.put("error",error);v.put("remote",remote==null?null:remote.toString());getWritableDatabase().update("outbox",v,"seq=?",new String[]{""+seq});}
    public synchronized void acknowledge(JSONObject op,long revision){acknowledge(op,revision,false);}
    synchronized void acknowledge(JSONObject op,long revision,boolean restoreAttribution){SQLiteDatabase d=getWritableDatabase();d.beginTransaction();try{ContentValues v=new ContentValues();v.put("revision",revision);d.update("events",v,"id=?",new String[]{op.optString("entity")});d.delete("outbox","seq=?",new String[]{""+op.optLong("seq")});
        JSONObject current=restoreAttribution?event(op.optString("entity")):null;
        if(current!=null&&current.optInt("deleted")==0&&!current.optString("caregiver_name").isEmpty()&&query("SELECT seq FROM outbox WHERE entity=? LIMIT 1",op.optString("entity")).isEmpty())queue(d,current,"upsert");
        d.setTransactionSuccessful();}finally{d.endTransaction();}}
    public synchronized void request(String id,String mode,String prompt){ContentValues v=new ContentValues();v.put("id",id);v.put("mode",mode);v.put("prompt",prompt);v.put("created",System.currentTimeMillis());v.put("status","queued");getWritableDatabase().insertOrThrow("requests",null,v);set("request_context_"+id,syncContext().toString());set("request_transport_"+id,get("request_provider_"+id,"").isEmpty()?"backend":"direct");}
    public synchronized boolean claimRequest(String id){ContentValues v=new ContentValues();v.put("status","running");return getWritableDatabase().update("requests",v,"id=? AND status='queued'",new String[]{id})==1;}
    public synchronized void requestState(String id,String status,String job,String text,String error){ContentValues v=new ContentValues();v.put("status",status);if(job!=null)v.put("job",job);if(text!=null)v.put("response",text);v.put("error",error==null?"":error);getWritableDatabase().update("requests",v,"id=? AND status!='cancelled'",new String[]{id});}
    public synchronized void cancelRequest(String id){ContentValues v=new ContentValues();v.put("status","cancelled");getWritableDatabase().update("requests",v,"id=?",new String[]{id});}
    public synchronized JSONObject requestById(String id){List<JSONObject> r=query("SELECT * FROM requests WHERE id=?",id);return r.isEmpty()?null:r.get(0);}
    public synchronized JSONObject latestRequest(String mode){List<JSONObject> r=query("SELECT * FROM requests WHERE mode=? ORDER BY created DESC LIMIT 1",mode);return r.isEmpty()?null:r.get(0);}
    public synchronized void reminder(String title,long due){if(title.trim().isEmpty()||title.length()>200||due<=System.currentTimeMillis())throw new IllegalArgumentException("请填写标题和未来时间");ContentValues v=new ContentValues();v.put("id",UUID.randomUUID().toString());v.put("title",title.trim());v.put("due",due);getWritableDatabase().insertOrThrow("reminders",null,v);}
    public synchronized void track(String id,String title,String category,String url,boolean local){ContentValues v=new ContentValues();v.put("id",id);v.put("title",title);v.put("category",category);v.put("url",url);v.put("local",local?1:0);getWritableDatabase().insertWithOnConflict("tracks",null,v,SQLiteDatabase.CONFLICT_REPLACE);}
    public synchronized void export(Writer writer)throws IOException{writer.write("\uFEFFid,type,start,end,amount,detail,note,timezone,revision,deleted,active,duration_ms,remote_json,caregiver_id,caregiver_name\r\n");try(Cursor c=getReadableDatabase().rawQuery("SELECT id,kind,start,end,amount,detail,note,timezone,revision,deleted,active,duration,remote_json,caregiver_id,caregiver_name FROM events ORDER BY start",null)){while(c.moveToNext()){for(int i=0;i<c.getColumnCount();i++){if(i>0)writer.write(',');writer.write(CareRules.csv(c.getString(i)));}writer.write("\r\n");}}writer.flush();}
}
