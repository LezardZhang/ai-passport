package cn.xigua.childcare;
public final class PlaybackRules {
    public static boolean resumeCreatesPlayer(String state,boolean hasPlayer){return !hasPlayer&&!"buffering".equals(state)&&!"buffering_paused".equals(state)&&!"playing".equals(state);}
}
