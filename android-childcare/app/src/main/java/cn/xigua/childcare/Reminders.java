package cn.xigua.childcare;
import android.app.*;
import android.content.*;
import org.json.JSONObject;
public final class Reminders {
    static final String CHANNEL="care-reminders";
    public static void channel(Context c){c.getSystemService(NotificationManager.class).createNotificationChannel(new NotificationChannel(CHANNEL,"照护提醒",NotificationManager.IMPORTANCE_DEFAULT));}
    public static PendingIntent intent(Context c,String id){return PendingIntent.getBroadcast(c,id.hashCode(),new Intent(c,ReminderReceiver.class).setAction("reminder").putExtra("id",id),PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE);}
    public static void scheduleAll(Context c){CareApp app=(CareApp)c.getApplicationContext();channel(c);for(JSONObject r:app.store.query("SELECT * FROM reminders WHERE done=0 ORDER BY due LIMIT 100")){long due=Math.max(System.currentTimeMillis()+1000,r.optLong("due"));c.getSystemService(AlarmManager.class).setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP,due,intent(c,r.optString("id")));}}
    public static void done(Context c,String id){CareApp a=(CareApp)c.getApplicationContext();a.store.getWritableDatabase().execSQL("UPDATE reminders SET done=1 WHERE id=?",new Object[]{id});c.getSystemService(AlarmManager.class).cancel(intent(c,id));c.getSystemService(NotificationManager.class).cancel(id.hashCode());a.changed();}
}
