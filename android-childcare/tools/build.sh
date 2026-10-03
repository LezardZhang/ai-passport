#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
TASK_SDK=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Library/Android/sdk}}
TASK_JAVA=${JAVA_HOME:-}
if [ -z "$TASK_JAVA" ]; then TASK_JAVA=$(find "$PWD/.local" -type d -path '*/Contents/Home' | head -1); fi
test -x "$TASK_JAVA/bin/javac" || { echo 'JDK 17 required (JAVA_HOME)'; exit 1; }
export JAVA_HOME="$TASK_JAVA"
export PATH="$TASK_JAVA/bin:$PATH"
TASK_BT="$TASK_SDK/build-tools/35.0.1"
TASK_PLATFORM="$TASK_SDK/platforms/android-35/android.jar"
rm -rf build/classes build/dex build/generated
mkdir -p build/classes build/dex build/generated build/apk .local/signing reports
TASK_VERSION=$(python3 -c 'import xml.etree.ElementTree as E; print(E.parse("app/src/main/AndroidManifest.xml").getroot().get("{http://schemas.android.com/apk/res/android}versionName"))')
case "$TASK_VERSION" in *[!a-zA-Z0-9.-]*|'') echo 'Invalid APK version'; exit 1 ;; esac
TASK_APK="build/apk/xigua-childcare-$TASK_VERSION.apk"
export TASK_APK
"$TASK_BT/aapt2" compile --dir app/src/main/res -o build/resources.zip
"$TASK_BT/aapt2" link -o build/base.apk --manifest app/src/main/AndroidManifest.xml -I "$TASK_PLATFORM" --java build/generated --auto-add-overlay -A app/src/main/assets build/resources.zip
find app/src/main/java build/generated -name '*.java' > build/sources.txt
javac -encoding UTF-8 -source 8 -target 8 -Xlint:-options -cp "$TASK_PLATFORM" -d build/classes @build/sources.txt
jar cf build/classes.jar -C build/classes .
"$TASK_BT/d8" --min-api 26 --lib "$TASK_PLATFORM" --output build/dex build/classes.jar
python3 - <<'PY'
from pathlib import Path
import zipfile
with zipfile.ZipFile('build/base.apk') as src, zipfile.ZipFile('build/universal-unsigned.apk','w') as out:
    for item in src.infolist(): out.writestr(item,src.read(item.filename))
    for dex in Path('build/dex').glob('*.dex'): out.write(dex,dex.name)
PY
"$TASK_BT/zipalign" -f -p 4 build/universal-unsigned.apk build/aligned.apk
test -f .local/signing/childcare-local.jks || { echo 'Restore the original signing keystore before building an update'; exit 1; }
"$TASK_BT/apksigner" sign --ks .local/signing/childcare-local.jks --ks-key-alias childcare-local --ks-pass pass:android --key-pass pass:android --out "$TASK_APK" build/aligned.apk
"$TASK_BT/apksigner" verify --verbose --print-certs "$TASK_APK" > reports/apk-signature.txt
"$TASK_BT/aapt" dump badging "$TASK_APK" > reports/apk-badging.txt
python3 - <<'PY'
import hashlib,json,datetime,os,xml.etree.ElementTree as E
from pathlib import Path
p=Path(os.environ['TASK_APK'])
manifest=E.parse('app/src/main/AndroidManifest.xml').getroot(); namespace='{http://schemas.android.com/apk/res/android}'
sources={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for root in ('app','tools','tests') for f in sorted(Path(root).rglob('*')) if f.is_file()}
Path('reports/build-manifest.json').write_text(json.dumps({'package':'cn.xigua.childcare','version_code':int(manifest.get(namespace+'versionCode')),'version_name':manifest.get(namespace+'versionName'),'min_sdk':26,'target_sdk':35,'apk':str(p),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'signing':'dedicated local development key; not production','built_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),'source_files':sources},indent=2)+'\n')
print('APK:',p,'SHA256:',hashlib.sha256(p.read_bytes()).hexdigest())
PY
cat reports/apk-signature.txt
