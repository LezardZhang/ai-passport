package cn.xigua.childcare;
import java.time.*;
public final class FamilyRules {
    public static String caregiverTitle(String name,String relation){String title=relation==null?"":relation.trim();return title.isEmpty()?(name==null?"":name.trim()):title;}
    public static String validateCaregiverTitle(String value){String title=value==null?"":value.trim();if(title.isEmpty()||title.length()>20)throw new IllegalArgumentException("请填写称谓（最多20字），例如爸爸、爷爷、奶奶");return title;}
    public static void validateChild(String name,String birthday,String timezone,LocalDate today){
        name(name);try{ZoneId.of(timezone);if(!birthday.trim().isEmpty()&&LocalDate.parse(birthday).isAfter(today))throw new IllegalArgumentException();}catch(Exception e){throw new IllegalArgumentException("请填写有效时区和生日，生日不能晚于今天");}
    }
    private static void name(String value){if(value==null||value.trim().isEmpty()||value.trim().length()>40)throw new IllegalArgumentException("姓名须为 1–40 字");}
    public static void validateMember(String name,String relation,String phone){name(name);if(relation==null||relation.trim().isEmpty()||relation.length()>20)throw new IllegalArgumentException("请填写称呼或关系（最多20字）");if(phone==null||(!phone.isEmpty()&&!phone.matches("[+0-9 ()-]{6,24}")))throw new IllegalArgumentException("联系电话格式无效，也可以留空");}
}
