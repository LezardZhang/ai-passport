package cn.xigua.childcare;
import java.io.*;
import java.nio.*;

/** Canonical 16kHz mono WAV owned by this app. File streaming, no recording time cutoff. */
final class SpeechAudio {
    static final int SAMPLE_RATE=16000,BYTES_PER_SECOND=32000,CHUNK_BYTES=1920000;
    static final long RECORD_LIMIT=64L*1024*1024;
    static byte[] header(int bytes){ByteBuffer b=ByteBuffer.allocate(44).order(ByteOrder.LITTLE_ENDIAN);b.put(new byte[]{'R','I','F','F'}).putInt(bytes+36).put(new byte[]{'W','A','V','E','f','m','t',' '}).putInt(16).putShort((short)1).putShort((short)1).putInt(SAMPLE_RATE).putInt(BYTES_PER_SECOND).putShort((short)2).putShort((short)16).put(new byte[]{'d','a','t','a'}).putInt(bytes);return b.array();}
    static long payloadBytes(File source)throws IOException{try(RandomAccessFile in=new RandomAccessFile(source,"r")){if(in.length()<44||in.length()>RECORD_LIMIT+44)throw new IOException("录音文件不可用");byte[] head=new byte[44];in.readFully(head);ByteBuffer b=ByteBuffer.wrap(head).order(ByteOrder.LITTLE_ENDIAN);long bytes=b.getInt(40)&0xffffffffL;if(b.getInt(0)!=0x46464952||b.getInt(8)!=0x45564157||b.getShort(20)!=1||b.getShort(22)!=1||b.getInt(24)!=SAMPLE_RATE||b.getShort(34)!=16||b.getInt(36)!=0x61746164||bytes!=in.length()-44||bytes%2!=0)throw new IOException("录音格式不可用");return bytes;}}
    static long writeChunk(File source,long offset,File dest)throws IOException{long bytes=payloadBytes(source);if(offset<0||offset>=bytes||offset%2!=0)throw new IOException("录音分段不可用");int amount=(int)Math.min(CHUNK_BYTES,bytes-offset);try(RandomAccessFile in=new RandomAccessFile(source,"r");OutputStream out=new BufferedOutputStream(new FileOutputStream(dest))){out.write(header(amount));in.seek(44+offset);byte[] buffer=new byte[8192];int remaining=amount;while(remaining>0){int n=in.read(buffer,0,Math.min(buffer.length,remaining));if(n<0)throw new EOFException("录音不完整");out.write(buffer,0,n);remaining-=n;}}return amount;}
}
