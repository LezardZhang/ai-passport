package cn.xigua.childcare;
import java.util.*;
public final class RequestOwner {
    private final Map<String,String> current=new HashMap<>();
    public synchronized String begin(String mode){String id=UUID.randomUUID().toString();current.put(mode,id);return id;}
    public synchronized void cancel(String mode){current.remove(mode);}
    public synchronized boolean accepts(String mode,String id){return id!=null&&id.equals(current.get(mode));}
}
