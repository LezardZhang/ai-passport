package cn.xigua.childcare;

/** Single visible attempt; Send waits for owned audio, never validates it as empty text. */
final class SpeechSession {
    enum Phase {IDLE,PERMISSION,RECORDING,TRANSCRIBING,READY,ERROR}
    enum Send {STOP_AND_WAIT,WAIT,TEXT,EMPTY}
    enum Result {IGNORED,READY,SUBMIT,ERROR}
    private long owner;private Phase phase=Phase.IDLE;private boolean pendingSend;
    long begin(){owner++;pendingSend=false;phase=Phase.RECORDING;return owner;}
    Phase phase(){return phase;}
    boolean accepts(long id){return owner==id&&(phase==Phase.RECORDING||phase==Phase.TRANSCRIBING||phase==Phase.PERMISSION);}
    void awaitPermission(long id){if(accepts(id))phase=Phase.PERMISSION;}
    void recording(long id){if(accepts(id))phase=Phase.RECORDING;}
    void transcribing(long id){if(accepts(id))phase=Phase.TRANSCRIBING;}
    Send send(boolean hasText){if(phase==Phase.RECORDING){pendingSend=true;phase=Phase.TRANSCRIBING;return Send.STOP_AND_WAIT;}if(phase==Phase.TRANSCRIBING||phase==Phase.PERMISSION){pendingSend=true;return Send.WAIT;}return hasText?Send.TEXT:Send.EMPTY;}
    Result complete(long id,String text){if(!accepts(id))return Result.IGNORED;if(text==null||text.trim().isEmpty()){fail(id);return Result.ERROR;}phase=Phase.READY;boolean send=pendingSend;pendingSend=false;return send?Result.SUBMIT:Result.READY;}
    void fail(long id){if(accepts(id)){phase=Phase.ERROR;pendingSend=false;}}
    void cancel(){owner++;phase=Phase.IDLE;pendingSend=false;}
}
