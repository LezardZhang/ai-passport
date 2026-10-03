#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ -z "${JAVA_HOME:-}" ]; then
  JAVA_HOME=$(find "$PWD/.local" -type d -path '*/Contents/Home' | head -1)
fi
mkdir -p build/host
"$JAVA_HOME/bin/javac" -encoding UTF-8 -d build/host app/src/main/java/cn/xigua/childcare/CareRules.java app/src/main/java/cn/xigua/childcare/RequestOwner.java app/src/main/java/cn/xigua/childcare/PlaybackRules.java app/src/main/java/cn/xigua/childcare/IntentRouter.java app/src/main/java/cn/xigua/childcare/FamilyRules.java app/src/main/java/cn/xigua/childcare/UpdateRules.java app/src/main/java/cn/xigua/childcare/SpeechSession.java app/src/main/java/cn/xigua/childcare/SpeechAudio.java tests/CoreTest.java app/src/main/java/cn/xigua/childcare/ProviderRules.java tests/SpeechCoreTest.java tests/ProviderCoreTest.java app/src/main/java/cn/xigua/childcare/CareTimeline.java tests/TimelineCoreTest.java
"$JAVA_HOME/bin/java" -cp build/host cn.xigua.childcare.CoreTest

"$JAVA_HOME/bin/java" -cp build/host cn.xigua.childcare.SpeechCoreTest
"$JAVA_HOME/bin/java" -cp build/host cn.xigua.childcare.ProviderCoreTest
"$JAVA_HOME/bin/java" -cp build/host cn.xigua.childcare.TimelineCoreTest
