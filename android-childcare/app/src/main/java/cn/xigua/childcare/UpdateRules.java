package cn.xigua.childcare;
import java.net.URI;
public final class UpdateRules {
    public static void token(String value){if(value.length()>2048||value.chars().anyMatch(c->c<32||c>126))throw new IllegalArgumentException("更新凭据无效");}
    public static void sameOrigin(String feed,String apk){https(feed);https(apk);URI a=URI.create(feed),b=URI.create(apk);int ap=a.getPort()<0?443:a.getPort(),bp=b.getPort()<0?443:b.getPort();if(!a.getHost().equalsIgnoreCase(b.getHost())||ap!=bp)throw new IllegalArgumentException("安装包须来自同一更新站点");}
    public static void https(String value){try{URI u=URI.create(value);if(!"https".equalsIgnoreCase(u.getScheme())||u.getHost()==null||u.getUserInfo()!=null||u.getFragment()!=null)throw new Exception();}catch(Exception e){throw new IllegalArgumentException("请输入有效 HTTPS 地址");}}
    public static void validate(String pkg,long version,int minSdk,String url,String hash,String installed,long current,int sdk){if(!installed.equals(pkg))throw new IllegalArgumentException("更新包不属于西瓜育儿");if(version<=current)throw new IllegalArgumentException("已是当前版本，或频道提供了旧版本");if(minSdk>sdk)throw new IllegalArgumentException("这个版本需要更新的 Android 系统");https(url);if(hash==null||!hash.matches("[a-fA-F0-9]{64}"))throw new IllegalArgumentException("更新清单缺少有效 SHA256");}
}
