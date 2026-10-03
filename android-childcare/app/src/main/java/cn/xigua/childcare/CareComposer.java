package cn.xigua.childcare;

import android.app.Activity;
import android.text.*;
import android.view.View;
import android.widget.*;
import java.util.function.Consumer;

/** Voice and text entry with one visible push-to-talk state per utterance. */
final class CareComposer {
 final LinearLayout view;final EditText input;private final TextView status;private final View mic,send,end,cancel;private final LinearLayout idleRow,recordingPanel;private final SpeechSession session=new SpeechSession();private final VoiceInput voice;private final Consumer<String> route,draft;private long currentOwner;private boolean large;
 CareComposer(Activity a,Ui u,VoiceInput voice,String initial,boolean large,Consumer<String> draft,Consumer<String> route){
  this.voice=voice;this.route=route;this.draft=draft;this.large=large;view=u.column();view.setPadding(u.dp(8),u.dp(8),u.dp(8),u.dp(8));view.setBackgroundColor(u.surface);
  idleRow=u.row();mic=u.iconAction("mic","开始语音输入",this::beginVoice);idleRow.addView(mic,new LinearLayout.LayoutParams(u.dp(48),u.dp(48)));
  input=u.input("说说刚发生的事…",initial,InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_FLAG_MULTI_LINE);input.setMinHeight(u.dp(48));input.setPadding(u.dp(10),u.dp(6),u.dp(10),u.dp(6));input.setMinLines(1);input.setMaxLines(3);input.setFilters(new InputFilter[]{new InputFilter.LengthFilter(12000)});idleRow.addView(input,new LinearLayout.LayoutParams(0,-2,1));send=u.iconAction("send","发送",this::send);idleRow.addView(send,new LinearLayout.LayoutParams(u.dp(48),u.dp(48)));view.addView(idleRow);
  recordingPanel=u.column();recordingPanel.setPadding(u.dp(8),u.dp(8),u.dp(8),u.dp(4));recordingPanel.setBackground(u.shape(u.soft,18));TextView heading=u.text("正在录音",22,u.ink,true);heading.setGravity(android.view.Gravity.CENTER);u.add(recordingPanel,heading,2);status=u.text("说完后点中间的结束",14,u.muted,false);status.setGravity(android.view.Gravity.CENTER);u.add(recordingPanel,status,6);
  end=u.button("结束录音",true,this::finishVoice);LinearLayout.LayoutParams ep=new LinearLayout.LayoutParams(-1,u.dp(64));ep.setMargins(u.dp(32),u.dp(12),u.dp(32),u.dp(8));recordingPanel.addView(end,ep);
  cancel=u.button("取消录音",false,this::cancelVoice);recordingPanel.addView(cancel,new LinearLayout.LayoutParams(-1,u.dp(48)));view.addView(recordingPanel);
  input.addTextChangedListener(new TextWatcher(){public void beforeTextChanged(CharSequence t,int x,int y,int z){}public void onTextChanged(CharSequence t,int x,int y,int z){draft.accept(t.toString());}public void afterTextChanged(Editable e){}});update();
 }
 private void beginVoice(){if(session.phase()==SpeechSession.Phase.RECORDING||session.phase()==SpeechSession.Phase.TRANSCRIBING||session.phase()==SpeechSession.Phase.PERMISSION)return;input.setText("");draft.accept("");currentOwner=session.begin();final long owner=currentOwner;VoiceInput.Listener listener=new VoiceInput.Listener(){public void state(String text){if(session.accepts(owner)){status.setText(text);update();}}public void phase(SpeechSession.Phase phase){if(phase==SpeechSession.Phase.PERMISSION)session.awaitPermission(owner);else if(phase==SpeechSession.Phase.RECORDING)session.recording(owner);else if(phase==SpeechSession.Phase.TRANSCRIBING)session.transcribing(owner);update();}public void transcript(String text){SpeechSession.Result result=session.complete(owner,text);if(result==SpeechSession.Result.IGNORED)return;if(result==SpeechSession.Result.ERROR){status.setText("没有得到文字，可以再次点话筒重试。");update();return;}input.setText(text);input.setSelection(input.length());status.setText("已转成文字，可以编辑或发送。");update();if(result==SpeechSession.Result.SUBMIT)route.accept(text);}public void failed(String text){if(session.accepts(owner)){session.fail(owner);status.setText(text);update();}}public void finished(){update();}};voice.start(listener);update();}
 private void finishVoice(){if(session.phase()==SpeechSession.Phase.RECORDING){session.transcribing(currentOwner);voice.stop();status.setText("正在转成文字…");update();}}
 private void cancelVoice(){session.cancel();voice.discard();input.setText("");draft.accept("");status.setText("");update();}
 private void send(){SpeechSession.Send action=session.send(!input.getText().toString().trim().isEmpty());if(action==SpeechSession.Send.STOP_AND_WAIT){finishVoice();return;}if(action==SpeechSession.Send.WAIT){status.setText("正在处理语音，完成后会发送。");update();return;}if(action==SpeechSession.Send.TEXT)route.accept(input.getText().toString().trim());else status.setText("先说一句，或输入文字。");update();}
 private void update(){SpeechSession.Phase p=session.phase();boolean recording=p==SpeechSession.Phase.RECORDING;boolean processing=p==SpeechSession.Phase.TRANSCRIBING||p==SpeechSession.Phase.PERMISSION;boolean showPanel=recording||processing;view.setKeepScreenOn(showPanel);idleRow.setVisibility(showPanel?View.GONE:View.VISIBLE);recordingPanel.setVisibility(showPanel?View.VISIBLE:View.GONE);input.setEnabled(!showPanel);mic.setContentDescription(showPanel?"正在录音":"开始语音输入");send.setContentDescription(showPanel?"识别完成后发送":"发送");end.setEnabled(recording);cancel.setEnabled(showPanel);status.setVisibility(showPanel||status.length()>0?View.VISIBLE:View.GONE);send.setEnabled(!showPanel&&input.getText().toString().trim().length()>0);}
 void cancel(){session.cancel();voice.cancel();update();}
}
