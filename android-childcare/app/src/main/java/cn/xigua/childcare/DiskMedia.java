package cn.xigua.childcare;
import android.content.Context;
import android.net.Uri;
import java.io.*;
import java.util.*;
final class DiskMedia {
    static final long ITEM_LIMIT=128L*1024*1024,CACHE_LIMIT=256L*1024*1024;
    static File dir(Context c){File d=new File(c.getFilesDir(),"audio");if(!d.isDirectory()&&!d.mkdirs())throw new IllegalStateException("音频目录不可写");return d;}
    static void cleanup(Context c){File[] fs=dir(c).listFiles();if(fs!=null)for(File f:fs)if(f.getName().endsWith(".part"))f.delete();File[] cached=c.getCacheDir().listFiles();if(cached!=null)for(File f:cached)if((f.getName().startsWith("speech-")||f.getName().startsWith("capture-")||f.getName().startsWith("asr-part-"))&&f.getName().endsWith(".wav")&&f.lastModified()<CareApp.COLD_START)f.delete();}
    static File importAudio(Context c,Uri uri)throws IOException{
        File dst=new File(dir(c),UUID.randomUUID()+".audio"),temp=new File(dst+".part");long total=0,available=CACHE_LIMIT-size(c);
        try(InputStream in=c.getContentResolver().openInputStream(uri);OutputStream out=new FileOutputStream(temp)){if(in==null)throw new IOException("无法读取音频");byte[] buf=new byte[32768];int n;while((n=in.read(buf))!=-1){total+=n;if(total>ITEM_LIMIT)throw new IOException("单个音频上限 128 MiB");if(total>available)throw new IOException("本地音频已达 256 MiB，请先删除不需要的音频");out.write(buf,0,n);}}
        catch(Exception e){temp.delete();throw e;}
        if(total==0){temp.delete();throw new IOException("空音频");}
        if(size(c)>CACHE_LIMIT){temp.delete();throw new IOException("本地音频已达 256 MiB，请先删除不需要的音频");}
        if(!temp.renameTo(dst)){temp.delete();throw new IOException("保存音频失败");}return dst;
    }
    static long size(Context c){long sum=0;File[] fs=dir(c).listFiles();if(fs!=null)for(File f:fs)sum+=f.length();return sum;}
    static File ambience(Context c)throws IOException{
        File dst=new File(dir(c),"soft-rain.wav");if(dst.isFile())return dst;int rate=44100,seconds=30,count=rate*seconds,bytes=count*2;
        if(size(c)+bytes+44>CACHE_LIMIT)throw new IOException("本地音频预算不足，请先删除不需要的音频");
        File temp=new File(dst+".part");try(DataOutputStream o=new DataOutputStream(new BufferedOutputStream(new FileOutputStream(temp)))){
            o.writeBytes("RIFF");le(o,bytes+36,4);o.writeBytes("WAVEfmt ");le(o,16,4);le(o,1,2);le(o,1,2);le(o,rate,4);le(o,rate*2,4);le(o,2,2);le(o,16,2);o.writeBytes("data");le(o,bytes,4);
            Random random=new Random(20261002);double smooth=0;for(int k=0;k<count;k++){smooth=.96*smooth+.04*(random.nextDouble()*2-1);double fade=Math.min(1,Math.min(k/(double)rate,(count-k)/(double)rate));le(o,(int)(smooth*10000*fade),2);}
        }if(!temp.renameTo(dst))throw new IOException("无法保存环境音");return dst;
    }
    private static void le(DataOutputStream o,int value,int count)throws IOException{for(int i=0;i<count;i++)o.writeByte((value>>>(i*8))&255);}
}
