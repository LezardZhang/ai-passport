"""Bounded UI smoke checks on an isolated emulator; never clears a real phone."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import time
import xml.etree.ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument("--serial", required=True)
a = p.parse_args()
if not a.serial.startswith("emulator-"):
    raise SystemExit("Synthetic UI smoke data is restricted to an isolated emulator.")
root = Path(__file__).resolve().parents[1]
adb = str(Path(os.environ.get("ANDROID_SDK_ROOT", Path.home()/"Library/Android/sdk"))/"platform-tools/adb")
out = root/"reports/screenshots"
out.mkdir(parents=True, exist_ok=True)
actions=[]

def run(*args):
    return subprocess.check_output([adb,"-s",a.serial,*args],timeout=25)

def nodes():
    run("shell","uiautomator","dump","/sdcard/xigua-smoke.xml")
    return list(ET.fromstring(run("shell","cat","/sdcard/xigua-smoke.xml")).iter("node"))

def tap(label):
    candidates=[n for n in nodes() if n.get("text")==label or n.get("content-desc")==label or n.get("content-desc")==label+"，已选中"]
    if not candidates:
        raise AssertionError("Missing control: "+label)
    n=candidates[-1]
    x1,y1,x2,y2=map(int,re.findall(r"\d+",n.get("bounds")))
    run("shell","input","tap",str((x1+x2)//2),str((y1+y2)//2))
    time.sleep(.6)
    actions.append(label)

def capture(name):
    nodes()
    (out/(name+".png")).write_bytes(run("exec-out","screencap","-p"))
    (root/"reports"/(name+".xml")).write_bytes(run("shell","cat","/sdcard/xigua-smoke.xml"))

def swipe_up():
    run("shell","input","swipe","540","1800","540","650","350")
    time.sleep(.4)

original_tap=tap
def tap(label):
    for attempt in range(8):
        available=[n for n in nodes() if n.get("text")==label or n.get("content-desc")==label or n.get("content-desc")==label+"，已选中"]
        if available:
            return original_tap(label)
        swipe_up()
    raise AssertionError("Missing control after scrolling: "+label)

def type_text(value):
    run("shell","input","text",value.replace(" ","%s"))
    run("shell","input","keyevent","KEYCODE_BACK")

def tap_field(label):
    # A filled input exposes its value, so tapping a static label does not focus it.
    for attempt in range(8):
        ns=nodes()
        for index,n in enumerate(ns):
            if n.get("text")==label:
                field=n if n.get("class")=="android.widget.EditText" else next((x for x in ns[index+1:] if x.get("class")=="android.widget.EditText"),None)
                if field is not None:
                    x1,y1,x2,y2=map(int,re.findall(r"\d+",field.get("bounds")))
                    if x2>x1 and y2>y1:
                        run("shell","input","tap",str((x1+x2)//2),str((y1+y2)//2));time.sleep(.3);actions.append(label);return
        swipe_up()
    raise AssertionError("Missing input: "+label)

# Only this app on the explicitly isolated emulator is reset.
run("shell","pm","clear","cn.xigua.childcare")
run("shell","am","start","-W","cn.xigua.childcare/.MainActivity")
time.sleep(1);capture("care-v3-home")
tap("手动记录 / 更多照护工具");tap("手动记录");tap("喂养");capture("care-v3-feeding-confirm")
tap("可选备注");type_text("draft-rotation-v3")
run("shell","settings","put","system","accelerometer_rotation","0")
run("shell","settings","put","system","user_rotation","1");time.sleep(1)
run("shell","settings","put","system","user_rotation","0");time.sleep(1)
assert any(n.get("text")=="draft-rotation-v3" for n in nodes()), "Record draft lost on rotation"
actions.append("record draft survived rotation");tap("保存")
tap("家庭");tap("登记照护者");tap("姓名或称呼");type_text("care-mom");tap("与宝宝的关系");type_text("parent");capture("care-v3-person-register");tap("保存")
assert any("care-mom" in n.get("text","") for n in nodes());actions.append("caregiver registration persisted")
tap("登记照护者");tap("姓名或称呼");type_text("care-dad");tap("与宝宝的关系");type_text("parent");tap("保存")
tap("切换为这位照护者");tap("停用");tap("确认");tap("恢复使用");tap("确认");capture("care-v3-family")
tap("编辑宝宝资料");tap("生日 yyyy-MM-dd（可留空）");type_text("2099-01-01");tap("保存")
assert any(n.get("text")=="2099-01-01" for n in nodes()), "Invalid profile closed or discarded draft"
actions.append("invalid future birthday retained editor")
tap("2099-01-01");run("shell","input","keycombination","113","29");type_text("2026-01-01");tap("保存")
tap("AI 服务与模型");capture("care-v3-services")
tap("从接口获取模型")
for attempt in range(12):
    if any("已获取" in n.get("text","") for n in nodes()):break
    time.sleep(1)
assert any("已获取" in n.get("text","") for n in nodes()), "Live model discovery did not produce a usable list"
actions.append("live model list displayed")
tap("选择对话模型")
assert any("mimo-" in n.get("text","") for n in nodes()), "Model picker missing returned IDs"
tap("取消");tap("添加一套服务")
for label,value in [("服务名称","UI-provider"),("HTTPS Base URL","https://provider.example/v1"),("API Key","fixture-only"),("对话模型ID","chat-manual"),("语音识别模型ID","asr-manual")]:
    tap_field(label)
    run("shell","input","keycombination","113","29")
    type_text(value)
tap("保存")
for attempt in range(10):
    if any(n.get("text")=="UI-provider" for n in nodes()):break
    swipe_up()
assert any(n.get("text")=="UI-provider" for n in nodes()), "Second profile not saved"
actions.append("second key URL profile and manual models persisted")
capture("care-v3-second-service")
run("shell","am","force-stop","cn.xigua.childcare");run("shell","am","start","-W","cn.xigua.childcare/.MainActivity");time.sleep(1)
tap("家庭");tap("AI 服务与模型");tap("UI-provider");actions.append("second profile survived restart")
tap("家庭")
tap("版本与更新");capture("care-v3-updates")
assert any("暂未接入线上更新" in n.get("text","") for n in nodes()), "No-channel state missing"
actions.append("update no-channel state accurate")
tap("家庭");tap("打开夜间模式");tap("照护");capture("care-v3-night")
run("shell","settings","put","system","font_scale","2.0")
run("shell","am","force-stop","cn.xigua.childcare");run("shell","am","start","-W","cn.xigua.childcare/.MainActivity");time.sleep(1);capture("care-v3-night-200-percent")
assert any(n.get("text")=="发送" for n in nodes()), "Composer send inaccessible at 200%"
actions.append("200 percent composer remains accessible")
run("shell","settings","put","system","font_scale","1.0")
run("shell","am","force-stop","cn.xigua.childcare");run("shell","am","start","-W","cn.xigua.childcare/.MainActivity");time.sleep(1)
tap("说话")
permission=[n for n in nodes() if "permission_allow_foreground_only_button" in n.get("resource-id", "")]
if permission:
    n=permission[0];x1,y1,x2,y2=map(int,re.findall(r"\d+",n.get("bounds")));run("shell","input","tap",str((x1+x2)//2),str((y1+y2)//2));time.sleep(.5)
tap("取消语音")
assert any(n.get("text")=="说话" for n in nodes()), "Capture control failed to reset"
actions.append("microphone cancel resets unified composer")
tap("手动记录 / 更多照护工具");tap("音频库");tap("播放离线轻雨声");time.sleep(1);capture("care-v3-playing");tap("暂停");tap("继续");time.sleep(11)
run("shell","am","force-stop","cn.xigua.childcare");run("shell","am","start","-W","cn.xigua.childcare/.MainActivity");time.sleep(1)
assert not any(n.get("text")=="暂停" for n in nodes()), "Playback autoplayed after process death"
actions.append("process restart does not autoplay")
tap("手动记录 / 更多照护工具");tap("音频库");tap("继续上次音频")
run("shell","input","keyevent","KEYCODE_HOME");time.sleep(2)
service=run("shell","dumpsys","activity","services","cn.xigua.childcare").decode()
assert "PlaybackService" in service and "isForeground=true" in service
(root/"reports/background-service-v3.txt").write_text(service)
run("shell","am","start","-W","cn.xigua.childcare/.MainActivity");time.sleep(1);tap("停止");tap("照护");capture("care-v3-final")
(root/"reports/memory-sample-v3.txt").write_bytes(run("shell","dumpsys","meminfo","cn.xigua.childcare"))
(root/"reports/native-ui-smoke-v3.json").write_text(json.dumps({"serial":a.serial,"status":"PASS","actions":actions,"limits":"Emulator synthetic family only; no personal speech, audible quality or production identity/update channel acceptance"},ensure_ascii=False,indent=2)+"\n")
print("PASS",len(actions),"native v3 UI/playback actions")
