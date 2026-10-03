package cn.xigua.childcare;
public final class ProviderCoreTest {
 static int n;
 static void valid(String url,String key){ProviderRules.validate("服务",url,key,"mimo-v2.6-flash","mimo-v2.5-asr","api-key");n++;}
 static void bad(String url,String key){try{valid(url,key);throw new AssertionError("invalid provider accepted");}catch(IllegalArgumentException expected){n++;}}
 public static void main(String[] args){
  valid("https://token-plan-cn.xiaomimimo.com/v1","tp-fixture-not-real");
  valid("https://provider.example/proxy/v1/","custom-fixture");
  valid("https://api.xiaomimimo.com/v1","sk-fixture");
  bad("https://api.xiaomimimo.com/v1","tp-fixture");
  bad("https://user:pass@provider.example/v1","key");
  bad("https://provider.example/v1?secret=leak","key");
  bad("http://provider.example/v1","key");bad("https://provider.example/v1","key\r\nInjected: header");
  System.out.println("PASS "+n+" provider configuration assertions");
 }
}
