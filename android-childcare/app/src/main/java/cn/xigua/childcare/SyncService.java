package cn.xigua.childcare;
import android.app.job.*;
import android.content.*;
import java.util.concurrent.atomic.AtomicBoolean;

public final class SyncService extends JobService {
    private final AtomicBoolean cancelled=new AtomicBoolean();
    public static void schedule(Context c){JobScheduler scheduler=c.getSystemService(JobScheduler.class);ComponentName component=new ComponentName(c,SyncService.class);
        scheduler.schedule(new JobInfo.Builder(901,component).setRequiredNetworkType(JobInfo.NETWORK_TYPE_ANY).setBackoffCriteria(30000,JobInfo.BACKOFF_POLICY_EXPONENTIAL).setPersisted(true).build());
        if(scheduler.getPendingJob(902)==null)scheduler.schedule(new JobInfo.Builder(902,component).setRequiredNetworkType(JobInfo.NETWORK_TYPE_ANY).setPeriodic(15*60000L).setPersisted(true).build());}
    @Override public boolean onStartJob(JobParameters params){cancelled.set(false);CareApp app=(CareApp)getApplication();if("1".equals(app.store.get("sync_paused","0"))){app.store.set("sync_status","同步已暂停");app.changed();jobFinished(params,false);return false;}app.network.execute(()->{Api api=new Api(app,cancelled);boolean success=api.sync();if(!cancelled.get())jobFinished(params,!success);});return true;}
    @Override public boolean onStopJob(JobParameters p){cancelled.set(true);return true;}
}
