package cn.xigua.childcare;
import android.content.Context;
import android.graphics.*;
import android.graphics.drawable.*;
import android.view.*;
import android.widget.*;

final class Ui {
    final Context c;
    final int bg,surface,ink,muted,green,soft,peach,line;
    Ui(Context c,boolean ignored){this.c=c;bg=color("#F7F5EF");surface=color("#FFFFFF");ink=color("#233B32");muted=color("#667267");green=color("#285747");soft=color("#E9F0E9");peach=color("#F4DDCB");line=color("#E6E9DF");}
    static int color(String hex){return Color.parseColor(hex);}
    int dp(float n){return (int)(n*c.getResources().getDisplayMetrics().density+.5f);}
    GradientDrawable shape(int color,float radius){GradientDrawable d=new GradientDrawable();d.setColor(color);d.setCornerRadius(dp(radius));return d;}
    LinearLayout column(){LinearLayout l=new LinearLayout(c);l.setOrientation(LinearLayout.VERTICAL);return l;}
    LinearLayout row(){LinearLayout l=new LinearLayout(c);l.setOrientation(LinearLayout.HORIZONTAL);l.setGravity(Gravity.CENTER_VERTICAL);return l;}
    TextView text(String content,float size,int color,boolean bold){TextView t=new TextView(c);t.setText(content);t.setTextSize(size);t.setTextColor(color);t.setFontFeatureSettings("tnum");if(bold)t.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));t.setLineSpacing(dp(3),1);return t;}
    TextView label(String content){return text(content,14,muted,false);}
    TextView title(String content){return text(content,22,ink,true);}
    void add(LinearLayout l,View v,int top){LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);p.topMargin=dp(top);l.addView(v,p);}
    LinearLayout card(LinearLayout parent,int color){LinearLayout l=column();l.setPadding(dp(12),dp(12),dp(12),dp(12));l.setBackground(shape(color,16));add(parent,l,10);return l;}
    TextView button(String text,boolean primary,Runnable click){TextView b=text(text,16,primary?Color.WHITE:green,true);b.setGravity(Gravity.CENTER);b.setPadding(dp(12),dp(8),dp(12),dp(8));b.setMinHeight(dp(48));b.setBackground(shape(primary?green:soft,12));b.setClickable(true);b.setFocusable(true);b.setContentDescription(text);b.setOnClickListener(v->click.run());return b;}
    EditText input(String hint,String value,int type){EditText e=new EditText(c);e.setSingleLine(false);e.setTextColor(ink);e.setHintTextColor(muted);e.setTextSize(16);e.setPadding(dp(14),dp(14),dp(14),dp(14));e.setBackground(shape(soft,16));e.setInputType(type);e.setHint(hint);e.setText(value);e.setMinHeight(dp(56));return e;}
    void section(LinearLayout p,String title,String sub){add(p,text(title,18,ink,true),20);if(sub!=null&&!sub.isEmpty())add(p,label(sub),4);}
    View icon(String kind,int color,int size){return new Icon(c,kind,color,dp(size));}
    private int assetFor(String kind){
        if("settings".equals(kind))return R.drawable.ic_settings;
        if("audio_library".equals(kind))return R.drawable.ic_audio_library;
        if("lullaby".equals(kind))return R.drawable.ic_lullaby;
        if("story".equals(kind))return R.drawable.ic_story;
        if("classical".equals(kind))return R.drawable.ic_classical;
        if("timer".equals(kind))return R.drawable.ic_timer;
        if("import".equals(kind))return R.drawable.ic_import;
        if("refresh".equals(kind))return R.drawable.ic_refresh;
        if("sync".equals(kind))return R.drawable.ic_sync;
        return 0;
    }
    View assetIcon(String kind,int size){int id=assetFor(kind);if(id==0)return icon(kind,green,size);ImageView image=new ImageView(c);image.setImageResource(id);image.setScaleType(ImageView.ScaleType.CENTER_INSIDE);image.setLayoutParams(new ViewGroup.LayoutParams(dp(size),dp(size)));image.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);return image;}
    View iconAction(String kind,String label,Runnable action){FrameLayout box=new FrameLayout(c);box.setMinimumWidth(dp(48));box.setMinimumHeight(dp(48));FrameLayout.LayoutParams p=new FrameLayout.LayoutParams(dp(30),dp(30),Gravity.CENTER);box.addView(assetIcon(kind,30),p);box.setBackground(new RippleDrawable(android.content.res.ColorStateList.valueOf(soft),null,shape(Color.WHITE,12)));box.setClickable(true);box.setFocusable(true);box.setContentDescription(label);box.setOnClickListener(v->action.run());return box;}
    private String itemIcon(String title){if(title.contains("音频")||title.contains("故事"))return "audio_library";if(title.contains("同步"))return "sync";if(title.contains("备份")||title.contains("导出"))return "import";if(title.contains("更新")||title.contains("刷新"))return "refresh";if(title.contains("提醒"))return "timer";if(title.contains("服务")||title.contains("设置"))return "settings";return "records";}
    LinearLayout item(LinearLayout parent,String title,String detail,String action,Runnable click){LinearLayout row=row();row.setPadding(dp(12),dp(4),dp(8),dp(4));row.setMinimumHeight(dp(56));row.setBackground(shape(surface,12));row.addView(assetIcon(itemIcon(title),24),new LinearLayout.LayoutParams(dp(32),dp(32)));LinearLayout labels=column();labels.addView(text(title,16,ink,true));if(detail!=null&&!detail.isEmpty())add(labels,text(detail,12,muted,false),2);row.addView(labels,new LinearLayout.LayoutParams(0,-2,1));if(action!=null&&!action.isEmpty())row.addView(button(action,false,click));else row.addView(icon("chevron",muted,20));row.setClickable(true);row.setFocusable(true);row.setContentDescription(title+(detail==null?"":"，"+detail)+(action==null?"":"，"+action));row.setOnClickListener(v->click.run());add(parent,row,4);return row;}
    static final class Icon extends View {
        final String kind;final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);final int size;
        Icon(Context c,String kind,int color,int size){super(c);this.kind=kind;this.size=size;p.setColor(color);p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(1.8f);p.setStrokeCap(Paint.Cap.ROUND);p.setStrokeJoin(Paint.Join.ROUND);setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);}
        @Override protected void onMeasure(int a,int b){setMeasuredDimension(size,size);}
        @Override protected void onDraw(Canvas c){c.save();c.scale(size/24f,size/24f);Path path=new Path();switch(kind){
        case "feeding":c.drawRoundRect(7,7,17,22,3,3,p);c.drawRect(9,3,15,7,p);c.drawLine(12,1,12,3,p);c.drawLine(8,12,12,12,p);c.drawLine(8,16,12,16,p);break;
        case "sleep":path.moveTo(17,2);path.cubicTo(2,0,1,20,14,22);path.cubicTo(20,23,23,16,22,14);path.cubicTo(14,17,10,7,17,2);c.drawPath(path,p);break;
        case "diaper":path.moveTo(12,2);path.cubicTo(9,8,4,11,5,16);path.cubicTo(7,24,20,24,19,15);path.cubicTo(18,10,14,6,12,2);c.drawPath(path,p);break;
        case "qa":c.drawRoundRect(2,3,22,18,4,4,p);c.drawLine(6,18,6,22,p);c.drawLine(6,22,11,18,p);c.drawLine(7,8,17,8,p);c.drawLine(7,12,14,12,p);break;
        case "story":c.drawRoundRect(4,2,20,22,2,2,p);c.drawLine(8,2,8,22,p);c.drawLine(11,7,17,7,p);c.drawLine(11,11,17,11,p);break;
        case "audio":c.drawLine(7,18,7,5,p);c.drawLine(7,5,20,2,p);c.drawLine(20,2,20,15,p);c.drawOval(2,16,7,21,p);c.drawOval(15,13,20,18,p);break;
        case "mic":c.drawRoundRect(9,2,15,15,3,3,p);path.moveTo(5,10);path.cubicTo(5,22,19,22,19,10);c.drawPath(path,p);c.drawLine(12,19,12,23,p);break;
        case "records":c.drawRoundRect(4,2,20,22,3,3,p);c.drawLine(8,7,16,7,p);c.drawLine(8,12,16,12,p);c.drawLine(8,17,14,17,p);break;
        case "my":c.drawCircle(12,7,4,p);c.drawArc(3,13,21,29,185,170,false,p);break;
        case "send":path.moveTo(5,12);path.lineTo(12,5);path.lineTo(19,12);c.drawPath(path,p);c.drawLine(12,5,12,21,p);break;
        case "stop":c.drawRoundRect(6,6,18,18,2,2,p);break;
        case "back":path.moveTo(15,5);path.lineTo(8,12);path.lineTo(15,19);c.drawPath(path,p);break;
        case "chevron":path.moveTo(9,6);path.lineTo(15,12);path.lineTo(9,18);c.drawPath(path,p);break;
        case "settings":c.drawCircle(12,12,4,p);c.drawCircle(12,12,8,p);for(int n=0;n<8;n++){double r=n*Math.PI/4;c.drawLine((float)(12+8*Math.cos(r)),(float)(12+8*Math.sin(r)),(float)(12+11*Math.cos(r)),(float)(12+11*Math.sin(r)),p);}break;
        case "plus":c.drawLine(12,4,12,20,p);c.drawLine(4,12,20,12,p);break;
        default:c.drawCircle(12,12,9,p);c.drawLine(12,6,12,12,p);c.drawLine(12,12,17,15,p);}
        c.restore();}
    }
}
