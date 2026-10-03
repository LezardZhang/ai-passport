package cn.xigua.childcare;
import android.app.*;
import android.content.*;
import android.Manifest;
import android.content.pm.PackageManager;
import android.os.Build;
import org.json.JSONObject;
import java.util.List;
public final class ReminderReceiver extends BroadcastReceiver {
    @Override public void onReceive(Context c,Intent i){PendingResult pending=goAsync();CareApp app=(CareApp)c.getApplicationContext();app.database.execute(()->{try{
        if(Intent.ACTION_BOOT_COMPLETED.equals(i.getAction())){Reminders.scheduleAll(c);return;}
        List<JSONObject> rows=app.store.query("SELECT * FROM reminders WHERE id=? AND done=0",i.getStringExtra("id"));if(rows.isEmpty())return;JSONObject r=rows.get(0);Reminders.channel(c);
        if(Build.VERSION.SDK_INT>=33&&c.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)!=PackageManager.PERMISSION_GRANTED)return;
        PendingIntent open=PendingIntent.getActivity(c,0,new Intent(c,MainActivity.class).putExtra("page","reminders"),PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);
        c.getSystemService(NotificationManager.class).notify(r.optString("id").hashCode(),new Notification.Builder(c,Reminders.CHANNEL).setSmallIcon(R.drawable.ic_app).setContentTitle(r.optString("title")).setContentText("照护提醒 · 点按查看或完成").setContentIntent(open).setAutoCancel(true).build());
    }finally{pending.finish();}});}
}
