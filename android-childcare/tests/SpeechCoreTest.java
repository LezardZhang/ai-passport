package cn.xigua.childcare;
import java.io.*;
import java.nio.file.*;

/** Real user sequences and WAV bytes, independent of Android and provider credentials. */
public final class SpeechCoreTest {
    static int n;
    static void equal(Object expected,Object actual){n++;if(!expected.equals(actual))throw new AssertionError("Expected "+expected+" got "+actual);}
    public static void main(String[] args)throws Exception{
        SpeechSession s=new SpeechSession();long a=s.begin();
        equal(SpeechSession.Send.STOP_AND_WAIT,s.send(false));
        equal(SpeechSession.Phase.TRANSCRIBING,s.phase());
        equal(SpeechSession.Send.WAIT,s.send(false));
        equal(SpeechSession.Result.SUBMIT,s.complete(a,"刚喂了150毫升"));
        equal(SpeechSession.Result.IGNORED,s.complete(a,"重复回调"));
        equal(SpeechSession.Send.TEXT,s.send(true));
        long b=s.begin();s.transcribing(b);
        equal(SpeechSession.Send.WAIT,s.send(false));
        s.cancel();equal(SpeechSession.Result.IGNORED,s.complete(b,"取消后迟到"));
        equal(SpeechSession.Send.EMPTY,s.send(false));
        long c=s.begin();s.send(false);s.fail(c);
        equal(SpeechSession.Phase.ERROR,s.phase());
        equal(SpeechSession.Result.IGNORED,s.complete(c,"失败后的迟到结果"));
        long d=s.begin();s.transcribing(d);equal(SpeechSession.Result.READY,s.complete(d,"重新识别成功"));
        equal(SpeechSession.Result.IGNORED,s.complete(c,"上次识别"));
        long e=s.begin();s.send(false);equal(SpeechSession.Result.ERROR,s.complete(e,"   "));
        equal(SpeechSession.Phase.ERROR,s.phase());
        long f=s.begin();s.awaitPermission(f);equal(SpeechSession.Send.WAIT,s.send(false));
        s.recording(f);equal(SpeechSession.Send.STOP_AND_WAIT,s.send(false));
        equal(SpeechSession.Result.SUBMIT,s.complete(f,"允许权限后发送"));
        equal(true,SpeechAudio.RECORD_LIMIT>SpeechAudio.BYTES_PER_SECOND*60L);
        File folder=Files.createTempDirectory("xigua-long-speech-").toFile();
        File source=new File(folder,"long.wav"),chunk=new File(folder,"chunk.wav");
        int payload=SpeechAudio.BYTES_PER_SECOND*65;
        try(OutputStream out=new BufferedOutputStream(new FileOutputStream(source))){out.write(SpeechAudio.header(payload));for(int k=0;k<payload;k++)out.write(k%251);}
        equal((long)payload,SpeechAudio.payloadBytes(source));
        long first=SpeechAudio.writeChunk(source,0,chunk);equal((long)SpeechAudio.CHUNK_BYTES,first);equal(first+44,chunk.length());
        try(RandomAccessFile in=new RandomAccessFile(chunk,"r")){in.seek(44);equal(0,in.read());in.seek(chunk.length()-1);equal((int)((first-1)%251),in.read());}
        long last=SpeechAudio.writeChunk(source,first,chunk);equal((long)payload-first,last);equal(first+last,(long)payload);
        try(RandomAccessFile in=new RandomAccessFile(chunk,"r")){in.seek(44);equal((int)(first%251),in.read());in.seek(chunk.length()-1);equal((payload-1)%251,in.read());}
        equal(last,SpeechAudio.payloadBytes(chunk));
        try{SpeechAudio.writeChunk(source,1,chunk);throw new AssertionError("unaligned PCM accepted");}catch(IOException expected){n++;}
        try(RandomAccessFile out=new RandomAccessFile(source,"rw")){out.seek(0);out.writeBytes("BAD!");}
        try{SpeechAudio.payloadBytes(source);throw new AssertionError("malformed WAV accepted");}catch(IOException expected){n++;}
        chunk.delete();source.delete();folder.delete();
        System.out.println("PASS "+n+" speech lifecycle/long WAV assertions");
    }
}
