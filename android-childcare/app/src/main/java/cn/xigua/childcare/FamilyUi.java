package cn.xigua.childcare;
import android.app.*;
import android.content.*;
import android.text.*;
import android.widget.*;
import org.json.JSONObject;
import java.time.*;

/** Local people registration. These records are attribution, never authentication. */
final class FamilyUi {
    private final Activity a;private final CareApp app;private final Ui u;private final Runnable refresh;private final SharedPreferences drafts;
    FamilyUi(Activity a,CareApp app,Ui u,Runnable refresh){this.a=a;this.app=app;this.u=u;this.refresh=refresh;drafts=a.getSharedPreferences("family_drafts",0);}
    void render(LinearLayout body){Store s=app.store;LinearLayout baby=u.card(body,u.surface);u.add(baby,u.text(s.get("child_name","西瓜")+"的档案",22,u.ink,true),0);String born=s.get("child_birthday","");u.add(baby,u.label(born.isEmpty()?"生日待完善": "生日 "+born),8);u.add(baby,u.label("1".equals(s.get("child_registered","0"))?"资料已登记 · 保存在本机":"原有记录已保留，请完善宝宝资料"),6);u.add(baby,u.button("编辑宝宝资料",false,this::editChild),14);
        u.section(body,"照护者","填一个称谓，记录就会显示是谁照顾了宝宝。");JSONObject selected=s.selectedMember();u.add(body,u.label(selected==null?"还未登记照护者": "当前照护者："+title(selected)),12);u.add(body,u.button("登记照护者",true,()->editMember(null)),12);
        for(JSONObject m:s.members()){LinearLayout card=u.card(body,u.surface);boolean active=m.optInt("active")==1,current=selected!=null&&selected.optString("id").equals(m.optString("id"));u.add(card,u.text(title(m)+(current?" · 当前照护者":active?"":" · 已停用"),19,u.ink,true),0);u.add(card,u.button("修改称谓",false,()->editMember(m)),12);if(active&&!current)u.add(card,u.button("切换为这位照护者",false,()->write(()->s.selectMember(m.optString("id")),"照护者已切换")),8);if(!current)u.add(card,u.button(active?"停用":"恢复使用",false,()->new AlertDialog.Builder(a).setTitle(active?"停用这位照护者？":"恢复这位照护者？").setMessage("历史记录和登记归属会保留。停用后不能选为新的登记人。").setNegativeButton("取消",null).setPositiveButton("确认",(d,w)->write(()->s.setMemberActive(m.optString("id"),!active),active?"已停用":"已恢复")).show()),8);}
    }
    private EditText field(LinearLayout fields,String key,String hint,String value,int type){EditText e=u.input(hint,drafts.getString(key,value),type);e.setFilters(new InputFilter[]{new InputFilter.LengthFilter(80)});e.addTextChangedListener(new TextWatcher(){public void beforeTextChanged(CharSequence s,int x,int y,int z){}public void onTextChanged(CharSequence s,int x,int y,int z){drafts.edit().putString(key,s.toString()).apply();}public void afterTextChanged(Editable s){}});u.add(fields,u.label(hint),12);u.add(fields,e,6);return e;}
    private LinearLayout fields(){LinearLayout l=u.column();l.setPadding(u.dp(20),u.dp(8),u.dp(20),u.dp(20));return l;}
    private ScrollView wrap(LinearLayout fields){ScrollView s=new ScrollView(a);s.addView(fields);return s;}
    private void editChild(){Store s=app.store;LinearLayout fields=fields();EditText name=field(fields,"child_name","宝宝昵称",s.get("child_name","西瓜"),1),birthday=field(fields,"child_birthday","生日 yyyy-MM-dd（可留空）",s.get("child_birthday",""),1);u.add(fields,u.button("选择生日",false,()->{LocalDate date;try{date=LocalDate.parse(birthday.getText().toString());}catch(Exception e){date=LocalDate.now();}DatePickerDialog picker=new DatePickerDialog(a,(view,y,m,d)->birthday.setText(LocalDate.of(y,m+1,d).toString()),date.getYear(),date.getMonthValue()-1,date.getDayOfMonth());picker.getDatePicker().setMaxDate(System.currentTimeMillis());picker.setButton(DialogInterface.BUTTON_NEUTRAL,"清除生日",(d,w)->birthday.setText(""));picker.show();}),8);u.add(fields,u.label("修改昵称或生日不会改变现有记录的时间与归属。"),12);AlertDialog dialog=new AlertDialog.Builder(a).setTitle("宝宝资料登记与更新").setView(wrap(fields)).setPositiveButton("保存",null).setNegativeButton("取消",null).create();dialog.setOnShowListener(d->dialog.getButton(-1).setOnClickListener(v->{String n=name.getText().toString(),b=birthday.getText().toString(),z=s.get("timezone","Asia/Shanghai");try{FamilyRules.validateChild(n,b,z,LocalDate.now(ZoneId.of(z)));saveDialog(dialog,()->s.saveChild(n,b,z),new String[]{"child_name","child_birthday"});}catch(Exception e){toast(e.getMessage());}}));dialog.show();}
    private static String title(JSONObject member){return FamilyRules.caregiverTitle(member.optString("name"),member.optString("relation"));}
    private void editMember(JSONObject previous){
        String id=previous==null?"new":previous.optString("id"),prefix="member_"+id+"_";
        LinearLayout fields=fields(),sentence=u.row();
        sentence.addView(u.text("我是孩子的",17,u.ink,false));
        String initial=drafts.getString(prefix+"relation",previous==null?"":title(previous));
        EditText relation=u.input("爸爸、爷爷、奶奶…",initial,android.text.InputType.TYPE_CLASS_TEXT);
        relation.setSingleLine(true);relation.setContentDescription("照护者称谓");
        relation.setFilters(new InputFilter[]{new InputFilter.LengthFilter(20)});
        relation.addTextChangedListener(new TextWatcher(){public void beforeTextChanged(CharSequence s,int x,int y,int z){}public void onTextChanged(CharSequence s,int x,int y,int z){drafts.edit().putString(prefix+"relation",s.toString()).apply();}public void afterTextChanged(Editable s){}});
        LinearLayout.LayoutParams input=new LinearLayout.LayoutParams(0,-2,1);input.leftMargin=u.dp(10);sentence.addView(relation,input);u.add(fields,sentence,8);
        AlertDialog dialog=new AlertDialog.Builder(a).setTitle(previous==null?"登记照护者":"修改称谓").setView(wrap(fields)).setPositiveButton("保存",null).setNegativeButton("取消",null).create();
        dialog.setOnShowListener(d->dialog.getButton(-1).setOnClickListener(v->{try{
            String r=FamilyRules.validateCaregiverTitle(relation.getText().toString());
            saveDialog(dialog,()->{if(previous==null)app.store.registerMember(r,r,"");else app.store.updateMember(id,r,r,previous.optString("phone"));},new String[]{prefix+"name",prefix+"relation",prefix+"phone"});
        }catch(Exception e){toast(e.getMessage());}}));dialog.show();
    }
    private interface Write{void run()throws Exception;}
    private void saveDialog(AlertDialog dialog,Write operation,String[] keys){dialog.getButton(-1).setEnabled(false);app.database.execute(()->{try{operation.run();app.main.post(()->{SharedPreferences.Editor edit=drafts.edit();for(String key:keys)edit.remove(key);edit.apply();dialog.dismiss();app.changed();toast("资料已保存");});}catch(Exception e){app.main.post(()->{dialog.getButton(-1).setEnabled(true);toast(e.getMessage());});}});}
    private void write(Write operation,String success){app.database.execute(()->{try{operation.run();app.main.post(()->{app.changed();toast(success);});}catch(Exception e){app.main.post(()->toast(e.getMessage()));}});}
    private void toast(String message){Toast.makeText(a,message==null?"未能保存，请重试":message,Toast.LENGTH_LONG).show();}
}
