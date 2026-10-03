package cn.xigua.childcare;
import java.io.*;
/** Compatible provider boundary also permits deterministic lifecycle acceptance. */
interface SpeechBackend {
 void ready()throws IOException;
 String transcribe(File wav,File scratch,MimoClient.Progress progress)throws Exception;
 void cancel();
}
