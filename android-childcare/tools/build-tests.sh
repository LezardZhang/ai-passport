#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
TASK_SDK=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Library/Android/sdk}}
TASK_JAVA=${JAVA_HOME:-}
if [ -z "$TASK_JAVA" ]; then TASK_JAVA=$(find "$PWD/.local" -type d -path '*/Contents/Home' | head -1); fi
export JAVA_HOME="$TASK_JAVA"
export PATH="$TASK_JAVA/bin:$PATH"
TASK_BT="$TASK_SDK/build-tools/35.0.1"
TASK_PLATFORM="$TASK_SDK/platforms/android-35/android.jar"
mkdir -p build/test-classes build/test-dex
"$TASK_BT/aapt2" link -o build/test-base.apk --manifest tests/AndroidManifest.xml -I "$TASK_PLATFORM"
javac -encoding UTF-8 -source 8 -target 8 -Xlint:-options -cp "$TASK_PLATFORM:build/classes.jar" -d build/test-classes tests/StoreInstrumentation.java tests/LiveProviderInstrumentation.java tests/TimelineInstrumentation.java tests/TimelineUiInstrumentation.java tests/UpdateInstrumentation.java tests/UpdateInstallInstrumentation.java
jar cf build/test-classes.jar -C build/test-classes .
"$TASK_BT/d8" --min-api 26 --lib "$TASK_PLATFORM" --classpath build/classes.jar --output build/test-dex build/test-classes.jar
python3 - <<'PY'
import zipfile
from pathlib import Path
with zipfile.ZipFile('build/test-base.apk') as src, zipfile.ZipFile('build/test-unsigned.apk','w') as out:
    for item in src.infolist(): out.writestr(item,src.read(item.filename))
    for dex in Path('build/test-dex').glob('*.dex'): out.write(dex,dex.name)
PY
"$TASK_BT/zipalign" -f -p 4 build/test-unsigned.apk build/test-aligned.apk
"$TASK_BT/apksigner" sign --ks .local/signing/childcare-local.jks --ks-key-alias childcare-local --ks-pass pass:android --key-pass pass:android --out build/apk/xigua-childcare-tests.apk build/test-aligned.apk
"$TASK_BT/apksigner" verify build/apk/xigua-childcare-tests.apk
