package cn.xigua.childcare;
import android.app.*;
import android.content.*;
import android.content.pm.PackageInfo;
import android.widget.*;
import org.json.JSONObject;
import java.io.File;
import java.text.DateFormat;
import java.util.Date;

/** Cloud discovery, verified download, then Android's explicit installation confirmation. */
final class UpdatesUi {
 private final Activity a;private final CareApp app;private final Ui u;
 UpdatesUi(Activity a,CareApp app,Ui u,Runnable refresh){this.a=a;this.app=app;this.u=u;}
 void render(LinearLayout body){try{
  PackageInfo p=Updates.installed(a);boolean busy=Updates.busy.get();
  LinearLayout card=u.card(body,u.surface);u.add(card,u.text("当前版本 "+p.versionName,21,u.ink,true),0);
  boolean connected=!app.store.get("update_channel","").isEmpty();
  u.add(card,u.label(app.store.get("update_status",connected?"已接入云端更新。":"在线更新尚未接入。")),10);
  long checked=0;try{checked=Long.parseLong(app.store.get("update_last_checked","0"));}catch(Exception ignored){}
  if(checked>0)u.add(card,u.label("上次检查："+DateFormat.getDateTimeInstance(DateFormat.SHORT,DateFormat.SHORT).format(new Date(checked))),8);
  if(connected){app.store.set("update_automatic","1");u.add(card,u.label("已开启自动检查。每次打开应用并在后台恢复时会检查更新，最多每 6 小时一次。"),12);u.add(card,u.label("发现新版本后会在这里显示；下载和安装仍由你确认。"),4);}
  String release=app.store.get("update_release","");String name=app.store.get("update_file","");boolean staged=!name.isEmpty()&&new File(Updates.dir(a),name).isFile();
  if(!release.isEmpty()){JSONObject r=Store.json(release);LinearLayout newer=u.card(body,u.soft);u.add(newer,u.text("新版本 "+r.optString("version_name"),19,u.ink,true),0);if(!r.optString("notes").isEmpty())u.add(newer,u.label(r.optString("notes")),8);if(!busy&&!staged)u.add(newer,u.button("下载更新",true,()->app.network.execute(()->Updates.download(app))),12);}
  if(busy)u.add(body,u.button("取消准备",false,()->Updates.cancel(app)),10);
  if(staged){JSONObject c=Store.json(app.store.get("update_candidate","{}"));u.add(body,u.label("已准备版本 "+c.optString("version_name")+"，安装包校验通过。"),16);if(!busy)u.add(body,u.button("确认安装更新",true,()->{try{Updates.install(a);}catch(Exception e){toast(e.getMessage());}}),12);}
  u.add(body,u.label("覆盖更新会保留本机记录和家庭资料。首次安装更新时，Android 可能要求允许此应用安装更新包。"),18);
  if(!busy)u.add(body,u.button("选择APK更新",false,()->a.startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("application/vnd.android.package-archive").addCategory(Intent.CATEGORY_OPENABLE),54)),24);
 }catch(Exception e){u.add(body,u.label("无法读取版本信息，请重试"),12);}}
 private void toast(String text){Toast.makeText(a,text==null?"操作未完成，请重试":text,Toast.LENGTH_LONG).show();}
}
