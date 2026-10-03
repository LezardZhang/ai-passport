package cn.xigua.childcare;
import java.time.*;
public final class TimelineCoreTest {
    static int count;static void eq(Object want,Object got){count++;if(!want.equals(got))throw new AssertionError(want+" != "+got);}
    public static void main(String[] args){
        eq("照护者未登记",CareTimeline.actor("null"));
        eq("爸爸",CareTimeline.actor("爸爸"));eq("照护者未登记",CareTimeline.actor(""));
        eq("喂奶 150 毫升",CareTimeline.action("feeding",150,"奶粉",0,false));
        eq("喂奶 99.5 毫升",CareTimeline.action("feeding",99.5,"母乳",0,false));
        eq("喂养 · 数量未登记",CareTimeline.action("feeding",0,"",0,false));
        eq("换尿布 · 大便",CareTimeline.action("diaper",0,"大便",0,false));
        eq("睡眠 · 进行中",CareTimeline.action("sleep",0,"",0,true));
        eq("睡眠 · 1 小时 5 分钟",CareTimeline.action("sleep",0,"",3900000,false));
        eq("时间未登记",CareTimeline.clock(0,ZoneId.of("Asia/Shanghai"),LocalDate.of(2026,10,2)));
        eq("08:30",CareTimeline.clock(Instant.parse("2026-10-02T00:30:00Z").toEpochMilli(),ZoneId.of("Asia/Shanghai"),LocalDate.of(2026,10,2)));
        eq("10月1日\n23:00",CareTimeline.clock(Instant.parse("2026-10-01T15:00:00Z").toEpochMilli(),ZoneId.of("Asia/Shanghai"),LocalDate.of(2026,10,2)));
        eq("diaper",CareRules.voiceKind("刚给宝宝上厕所了"));
        eq("record",IntentRouter.classify("刚给宝宝上厕所了").action);
        System.out.println("PASS "+count+" timeline assertions");
    }
}
