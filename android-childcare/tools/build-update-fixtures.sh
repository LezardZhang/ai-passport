#!/bin/sh
# Only synthetic newer versions; never install these as a delivered release.
set -eu
cd "$(dirname "$0")/.."
TASK_SDK=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Library/Android/sdk}}
TASK_JAVA=${JAVA_HOME:-}
if [ -z "$TASK_JAVA" ]; then TASK_JAVA=$(find "$PWD/.local" -type d -path '*/Contents/Home' | head -1); fi
export JAVA_HOME="$TASK_JAVA"
export PATH="$TASK_JAVA/bin:$PATH"
TASK_BT="$TASK_SDK/build-tools/35.0.1"
mkdir -p build/update-fixtures .local/signing
python3 - <<'PY'
from pathlib import Path
import re
s=Path('app/src/main/AndroidManifest.xml').read_text()
code=int(re.search(r'android:versionCode="(\d+)"',s)[1])+1
s=re.sub(r'android:versionCode="\d+"',f'android:versionCode="{code}"',s)
s=re.sub(r'android:versionName="[^"]+"',f'android:versionName="{code}-test-only"',s)
Path('build/update-fixtures/AndroidManifest.xml').write_text(s)
PY
"$TASK_BT/aapt2" link -o build/update-fixtures/base.apk --manifest build/update-fixtures/AndroidManifest.xml -I "$TASK_SDK/platforms/android-35/android.jar" --auto-add-overlay -A app/src/main/assets build/resources.zip
python3 - <<'PY'
import zipfile
from pathlib import Path
with zipfile.ZipFile('build/update-fixtures/base.apk') as src, zipfile.ZipFile('build/update-fixtures/unsigned.apk','w') as out:
    for item in src.infolist(): out.writestr(item,src.read(item.filename))
    for dex in Path('build/dex').glob('*.dex'): out.write(dex,dex.name)
PY
"$TASK_BT/zipalign" -f -p 4 build/update-fixtures/unsigned.apk build/update-fixtures/aligned.apk
"$TASK_BT/apksigner" sign --ks .local/signing/childcare-local.jks --ks-key-alias childcare-local --ks-pass pass:android --out build/update-fixtures/candidate.apk build/update-fixtures/aligned.apk
if [ ! -f .local/signing/update-wrong-test.jks ]; then
  keytool -genkeypair -keystore .local/signing/update-wrong-test.jks -storepass android -keypass android -alias fixture -dname 'CN=Wrong Update Test Only' -keyalg RSA -keysize 2048 -validity 365
fi
"$TASK_BT/apksigner" sign --ks .local/signing/update-wrong-test.jks --ks-key-alias fixture --ks-pass pass:android --out build/update-fixtures/wrong-signer.apk build/update-fixtures/aligned.apk
