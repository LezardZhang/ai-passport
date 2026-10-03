package cn.xigua.childcare;
import java.net.URI;
final class ProviderRules {
 static void validate(String name,String base,String key,String chat,String asr,String auth){
  if(name==null||name.trim().isEmpty()||name.length()>40)throw new IllegalArgumentException("请填写40字内的服务名称");
  try{URI u=URI.create(base);if(base.length()>2048||!"https".equalsIgnoreCase(u.getScheme())||u.getHost()==null||u.getUserInfo()!=null||u.getQuery()!=null||u.getFragment()!=null)throw new Exception();
   if(key!=null&&key.startsWith("tp-")&&"api.xiaomimimo.com".equalsIgnoreCase(u.getHost()))throw new IllegalArgumentException("Token Plan密钥请配套使用专用URL");
  }catch(IllegalArgumentException e){throw e;}catch(Exception e){throw new IllegalArgumentException("请输入不含账号或查询参数的HTTPS Base URL");}
  if(key==null||key.length()>4096||key.indexOf('\r')>=0||key.indexOf('\n')>=0)throw new IllegalArgumentException("API Key格式不可用");
  for(String model:new String[]{chat,asr})if(model==null||model.length()>200||model.indexOf('\n')>=0||model.indexOf('\r')>=0)throw new IllegalArgumentException("模型ID最多200字");
  if(!"api-key".equals(auth)&&!"bearer".equals(auth))throw new IllegalArgumentException("请选择鉴权方式");
 }
}
