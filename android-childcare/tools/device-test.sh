#!/bin/sh
# Automated fixtures mutate synthetic state and therefore run only on an emulator.
set -eu
cd "$(dirname "$0")/.."
TASK_SERIAL=${1:-}
case "$TASK_SERIAL" in emulator-*) ;; *) echo 'Usage: tools/device-test.sh emulator-NNNN (isolated emulator only)'; exit 2 ;; esac
TASK_SDK=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Library/Android/sdk}}
TASK_ADB="$TASK_SDK/platform-tools/adb"
TASK_APK=$(python3 -c 'import json; print(json.load(open("reports/build-manifest.json"))["apk"])')
./tools/build-tests.sh
./tools/build-update-fixtures.sh
"$TASK_ADB" -s "$TASK_SERIAL" install -r "$TASK_APK"
"$TASK_ADB" -s "$TASK_SERIAL" install -r build/apk/xigua-childcare-tests.apk
"$TASK_ADB" -s "$TASK_SERIAL" push build/update-fixtures/candidate.apk /data/local/tmp/xigua-update-candidate.apk
"$TASK_ADB" -s "$TASK_SERIAL" push build/update-fixtures/wrong-signer.apk /data/local/tmp/xigua-update-wrong.apk
"$TASK_ADB" -s "$TASK_SERIAL" push build/apk/xigua-childcare-tests.apk /data/local/tmp/xigua-wrong-package.apk
"$TASK_ADB" -s "$TASK_SERIAL" push build/apk/xigua-childcare-0.1.0-local.apk /data/local/tmp/xigua-old-version.apk
"$TASK_ADB" -s "$TASK_SERIAL" shell pm grant cn.xigua.childcare android.permission.RECORD_AUDIO
"$TASK_ADB" -s "$TASK_SERIAL" shell am instrument -w cn.xigua.childcare.test/cn.xigua.childcare.StoreInstrumentation > reports/android-tests-v5.log
python3 - <<'PY'
from pathlib import Path
import re
log=Path('reports/android-tests-v5.log').read_text()
print(log)
if not re.search(r'^PASS \d+ Android assertions$',log,re.M):
    raise SystemExit('Android instrumentation did not pass')
PY
