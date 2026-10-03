package cn.xigua.childcare;
import android.content.Context;import android.graphics.*;import android.view.View;
/** Decorative continuous rail. All facts are exposed by the adjacent card. */
final class TimelineRail extends View {
    private final Ui ui;private final boolean first,last;private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    TimelineRail(Context c,Ui ui,boolean first,boolean last){super(c);this.ui=ui;this.first=first;this.last=last;setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);}
    @Override protected void onDraw(Canvas canvas){float x=getWidth()/2f,y=ui.dp(28);paint.setColor(ui.line);paint.setStrokeWidth(ui.dp(2));canvas.drawLine(x,first?y:0,x,last?y:getHeight(),paint);paint.setColor(ui.green);canvas.drawCircle(x,y,ui.dp(4),paint);}
}
