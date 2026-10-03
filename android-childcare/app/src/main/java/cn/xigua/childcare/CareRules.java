package cn.xigua.childcare;
import java.time.*;
import java.net.URI;
import java.util.Arrays;
public final class CareRules {
    public static void validateAt(String kind,double amount,long start,long end,String note,long now){validate(kind,amount,start,end,note);if(start>now||end>now)throw new IllegalArgumentException("记录时间不能晚于现在，未来事项请使用提醒");if(end>0&&end-start>864000000L)throw new IllegalArgumentException("单段时长不能超过 10 天");}
    public static String voiceKind(String text){if(text.contains("奶")||text.contains("喂")||text.contains("母乳")||text.contains("辅食"))return "feeding";if(text.contains("尿")||text.contains("便")||text.contains("上厕所"))return "diaper";if(text.contains("睡"))return "sleep";if(text.contains("洗澡"))return "bath";if(text.contains("趴"))return "tummy";return "";}
    public static double voiceQuantity(String text){java.util.regex.Matcher arabic=java.util.regex.Pattern.compile("(\\d{1,4}(?:\\.\\d{1,2})?)\\s*(?:毫升|[mM][lL])").matcher(text);if(arabic.find())return Double.parseDouble(arabic.group(1));java.util.regex.Matcher chinese=java.util.regex.Pattern.compile("([零〇一二两三四五六七八九十百千]+(?:点[零〇一二两三四五六七八九]+)?)\\s*毫升").matcher(text);if(!chinese.find())return 0;String[] parts=chinese.group(1).split("点");int sum=0,number=0;for(char c:parts[0].toCharArray()){int unit=c=='十'?10:c=='百'?100:c=='千'?1000:0;if(unit>0){sum+=(number==0?1:number)*unit;number=0;}else number=number*10+digit(c);}double result=sum+number;if(parts.length>1){double place=.1;for(char c:parts[1].toCharArray()){result+=digit(c)*place;place/=10;}}return java.math.BigDecimal.valueOf(result).setScale(2,java.math.RoundingMode.HALF_UP).doubleValue();}
    private static int digit(char c){switch(c){case '一':return 1;case '二':case '两':return 2;case '三':return 3;case '四':return 4;case '五':return 5;case '六':return 6;case '七':return 7;case '八':return 8;case '九':return 9;default:return 0;}}
    public static int voiceAmount(String text){java.util.regex.Matcher m=java.util.regex.Pattern.compile("(\\d{1,4})\\s*(?:毫升|[mM][lL])").matcher(text);return m.find()?Integer.parseInt(m.group(1)):0;}
    public static long[] day(LocalDate date,ZoneId zone){return new long[]{date.atStartOfDay(zone).toInstant().toEpochMilli(),date.plusDays(1).atStartOfDay(zone).toInstant().toEpochMilli()};}
    public static long overlap(long start,long end,long from,long to){return Math.max(0,Math.min(end,to)-Math.max(start,from));}
    public static void validate(String kind,double amount,long start,long end,String note){
        if(!Arrays.asList("feeding","sleep","diaper","bath","tummy","timer","note").contains(kind))throw new IllegalArgumentException("请选择记录类型");
        if(start<=0||end<0||(end>0&&end<=start))throw new IllegalArgumentException("结束时间须晚于开始时间");
        if(!Double.isFinite(amount)||amount<0||amount>2000)throw new IllegalArgumentException("喂养数量须为 0–2000 毫升（0 表示未知）");
        if(note!=null&&note.length()>2000)throw new IllegalArgumentException("备注最多 2000 字");
    }
    public static String quantity(double amount){return java.math.BigDecimal.valueOf(amount).stripTrailingZeros().toPlainString();}
    public static String csv(String value){
        String s=value==null?"":value;
        int first=0;while(first<s.length()&&Character.isWhitespace(s.charAt(first)))first++;
        if(first<s.length()&&"=+@-".indexOf(s.charAt(first))>=0)s="'"+s;
        return "\""+s.replace("\"","\"\"")+"\"";
    }
    public static boolean sameOrigin(String base,String media){try{
        URI a=URI.create(base), b=URI.create(media);
        return a.getScheme()!=null&&a.getHost()!=null&&a.getScheme().equalsIgnoreCase(b.getScheme())&&a.getHost().equalsIgnoreCase(b.getHost())&&port(a)==port(b)&&b.getUserInfo()==null;
    }catch(Exception e){return false;}}
    private static int port(URI u){return u.getPort()>=0?u.getPort():"https".equalsIgnoreCase(u.getScheme())?443:80;}
}
