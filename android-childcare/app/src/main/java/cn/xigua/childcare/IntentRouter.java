package cn.xigua.childcare;

/** Conservative offline command routing. Domain words alone never authorize a write. */
public final class IntentRouter {
    public final String action,kind,category;
    private IntentRouter(String action,String kind,String category){this.action=action;this.kind=kind;this.category=category;}
    private static IntentRouter result(String action){return new IntentRouter(action,"","");}
    private static boolean has(String s,String... words){for(String w:words)if(s.contains(w))return true;return false;}
    public static IntentRouter classify(String text){
        String s=text==null?"":text.trim();
        if(s.isEmpty()||s.length()>12000)return result("clarify");
        if(has(s,"今天","今日")&&has(s,"多少","几次","多久","统计","查看记录"))return result("records");
        if(has(s,"为什么","怎么办","合适","正常吗","如何","怎么","怎样","为何","是不是","能不能","是否","建议","可以吗","吗","？","?"))return result("qa");
        if(has(s,"不想","不要","不需要","不打算","不用","不必","不希望","无需","无须","取消","并","然后","再帮我","同时")||s.matches(".+(?:，|,|、|再)(?:播放|继续|暂停|停止|开始|讲|检查|登记|修改|提醒|更新|交接|记).*" )||s.matches(".*(?:别|不|没(?:有)?)(?:给我|帮我|再|先|要|想)*(?:播放|开始|记录|讲|放|听|喂|睡).*"))return result("clarify");
        boolean story=has(s,"故事")&&has(s,"讲","编","听","生成"),audio=has(s,"播放","放点","听首")||s.matches("^(?:想听|听|来点)?(?:雨声|白噪音|儿歌|古典音乐)[。！!]*$");
        boolean record=CareRules.voiceQuantity(s)>0||has(s,"记录","记一下","记下","刚","换了","洗了","喝了","喂了","补记","睡着了","开始睡眠","醒了","睡醒","结束睡眠")||s.matches(".*\\d+(?:\\.\\d+)?\\s*(?:毫升|ml).*" );
        int actions=(story?1:0)+(audio?1:0)+(record?1:0)+(has(s,"提醒")?1:0);if(actions>1)return result("clarify");
        if(has(s,"明天","后天","下周")&&!has(s,"提醒"))return result("clarify");
        if(has(s,"提醒"))return result("reminder");
        if(has(s,"交接"))return result("handoff");
        if(has(s,"登记照护者","家庭成员","宝宝档案","修改昵称"))return result("family");
        if(has(s,"更新应用","检查版本","升级应用"))return result("updates");
        if(has(s,"停止播放","关掉声音","停止朗读"))return result("stop");
        if(has(s,"暂停播放","暂停声音","暂停朗读"))return result("pause");
        if(has(s,"继续播放","继续朗读"))return result("resume");
        if(story)return result("story");
        if(audio)return new IntentRouter("audio","",has(s,"儿歌")?"song":has(s,"古典")?"classical":has(s,"故事")?"story":"white_noise");
        String kind=CareRules.voiceKind(s);if(kind.isEmpty()&&has(s,"醒了","睡醒"))kind="sleep";
        int domains=0;for(String domain:new String[]{"feeding","diaper","sleep","bath","tummy"}){boolean found="feeding".equals(domain)?has(s,"奶","喂"):"diaper".equals(domain)?has(s,"尿","便","上厕所"):"sleep".equals(domain)?has(s,"睡","醒"):"bath".equals(domain)?has(s,"洗澡"):has(s,"趴");if(found)domains++;}if(domains>1)return result("clarify");
        if("sleep".equals(kind)&&historical(s))return new IntentRouter("record","sleep","");
        if(has(s,"醒了","睡醒","结束睡眠"))return result("sleep_end");
        if("sleep".equals(kind)&&has(s,"睡着了","开始睡眠","睡了"))return result("sleep_start");
        if(!kind.isEmpty()&&(CareRules.voiceQuantity(s)>0||has(s,"记录","记一下","记下","刚","换了","洗了","喝了","喂了","补记")||s.matches(".*\\d+(?:\\.\\d+)?\\s*(?:毫升|ml).*")))return new IntentRouter("record",kind,"");
        return result("qa");
    }
    public static boolean historical(String text){return has(text,"昨天","前天","之前","上周","补记","分钟前","小时前")||text.matches(".*\\d{1,2}[:：点时].*");}
}
