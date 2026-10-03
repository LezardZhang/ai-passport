package cn.xigua.childcare;
import java.time.*;
import java.time.format.DateTimeFormatter;
/** Pure presentation of confirmed facts; unknown values stay explicit. */
public final class CareTimeline {
    public static String actor(String name){return name==null||name.trim().isEmpty()||"null".equals(name)?"照护者未登记":name;}
    public static String clock(long instant,ZoneId zone,LocalDate day){if(instant<=0)return "时间未登记";ZonedDateTime at=Instant.ofEpochMilli(instant).atZone(zone);String clock=at.format(DateTimeFormatter.ofPattern("HH:mm"));return at.toLocalDate().equals(day)?clock:at.format(DateTimeFormatter.ofPattern("M月d日"))+"\n"+clock;}
    public static String action(String kind,double amount,String detail,long duration,boolean active){
        switch(kind){
        case "feeding":return amount>0?"喂奶 "+CareRules.quantity(amount)+" 毫升":"喂养 · 数量未登记";
        case "diaper":return "换尿布"+(detail==null||detail.isEmpty()?" · 类型未登记":" · "+detail);
        case "sleep":return "睡眠 · "+(active?"进行中":duration>0?duration(duration):"时长未登记");
        case "bath":return "洗澡";case "tummy":return "趴玩"+(duration>0?" · "+duration(duration):"");case "timer":return "计时"+(duration>0?" · "+duration(duration):"");default:return "照护记录";
        }
    }
    private static String duration(long ms){long minutes=ms/60000;return minutes>=60?minutes/60+" 小时 "+minutes%60+" 分钟":minutes+" 分钟";}
}
