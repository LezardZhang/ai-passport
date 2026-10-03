package cn.xigua.childcare;

import android.app.Application;
import android.os.Handler;
import android.os.Looper;
import java.util.concurrent.*;
import java.util.*;

public final class CareApp extends Application {
    static final long COLD_START=System.currentTimeMillis();
    public Store store;
    public final ExecutorService database=Executors.newSingleThreadExecutor();
    public final ExecutorService speech=Executors.newSingleThreadExecutor();
    public final ExecutorService network=Executors.newSingleThreadExecutor();
    public final Handler main=new Handler(Looper.getMainLooper());
    private final Set<Runnable> observers=new HashSet<>();
    @Override public void onCreate(){super.onCreate();store=new Store(this);bootstrapFamilyCloud();SyncService.schedule(this);database.execute(()->{DiskMedia.cleanup(this);Updates.cleanup(this);Updates.initialize(this);Updates.maybeCheck(this);new Providers(this,store).all();store.getWritableDatabase().execSQL("UPDATE requests SET status='failed',error='上次生成中断，可重试原内容' WHERE status='running' AND job=''");store.getWritableDatabase().execSQL("DELETE FROM meta WHERE key GLOB 'request_provider_*' AND substr(key,18) IN (SELECT id FROM requests WHERE status IN ('failed','cancelled','completed'))");store.getWritableDatabase().execSQL("UPDATE requests SET status='failed',error='旧请求缺少原服务配置，请确认后重新发起' WHERE job='' AND status IN ('queued','pending') AND id NOT IN (SELECT substr(key,18) FROM meta WHERE key GLOB 'request_provider_*' AND value!='')");
            store.getWritableDatabase().execSQL("INSERT OR REPLACE INTO meta(key,value) SELECT 'request_transport_'||id,'direct' FROM requests WHERE job='' AND status IN ('queued','pending') AND id IN (SELECT substr(key,18) FROM meta WHERE key GLOB 'request_provider_*' AND value!='')");Reminders.scheduleAll(this);});}
    private void bootstrapFamilyCloud(){try(java.io.InputStream in=getAssets().open("family-cloud-config.json")){byte[] b=new byte[4096];int n=in.read(b);if(n<=0)return;org.json.JSONObject cfg=new org.json.JSONObject(new String(b,0,n,"UTF-8"));String base=cfg.optString("base_url").replaceAll("/+$","");String token=cfg.optString("token");if(!base.isEmpty()&&store.get("base_url","").isEmpty())store.set("base_url",base);if(!token.isEmpty()&&store.get("token","").isEmpty())store.set("token",token);if(store.get("base_url","").equals(base)&&token.isEmpty()&&store.get("token","").isEmpty())store.set("sync_status","家庭服务器已内置，等待育儿凭据");}catch(Exception ignored){store.set("sync_status","家庭服务器配置不可用");}}
    public void observe(Runnable r){observers.add(r);}
    public void remove(Runnable r){observers.remove(r);}
    public void changed(){main.post(()->{for(Runnable r:new ArrayList<>(observers))r.run();});}
}
