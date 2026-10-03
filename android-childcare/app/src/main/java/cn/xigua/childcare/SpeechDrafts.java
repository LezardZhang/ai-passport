package cn.xigua.childcare;
import android.content.Context;
import java.io.*;
final class SpeechDrafts {
 static File file(Context c){File dir=new File(c.getFilesDir(),"voice");if(!dir.isDirectory()&&!dir.mkdirs())throw new IllegalStateException("录音保存目录不可写");return new File(dir,"draft.wav");}
 static boolean exists(Context c){return file(c).isFile();}
 static void keep(Context c,File wav)throws IOException{File dst=file(c);if(!wav.renameTo(dst))throw new IOException("录音无法保存");}
 static void discard(Context c){file(c).delete();}
}
