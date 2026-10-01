"""Import verified, attributed PCM nursery audio through the deployed admin API."""
import hashlib
import json
from pathlib import Path
import subprocess
import wave

TARGET='cockpit-cloud-cloud-backup-1'


def request(path,method='GET',data=None):
    # The token stays inside the existing service container, never in argv/output.
    code="""import os,sys,json,urllib.request
r=urllib.request.Request('http://127.0.0.1:8080/childcare'+sys.argv[1],
 data=sys.stdin.buffer.read() if sys.argv[2]!='GET' else None,
 headers={'Authorization':'Bearer '+os.environ['CLOUDBACKUP_ADMIN_TOKEN'],
 'Content-Type':sys.argv[3]},method=sys.argv[2])
print(urllib.request.urlopen(r,timeout=60).read().decode())
"""
    binary=isinstance(data,bytes)
    body=data if binary else json.dumps(data).encode() if data is not None else b''
    result=subprocess.run(['docker','exec','-i',TARGET,'python','-c',code,path,method,
        'audio/wav' if binary else 'application/json'],input=body,stdout=subprocess.PIPE,check=True)
    return json.loads(result.stdout)


def main():
    source=Path(__file__).resolve().parents[1]/'seed-audio'
    manifest=json.loads((source/'manifest.json').read_text())
    existing={r['id']:r for r in request('/admin/api/audio/all')['items']}
    for item in manifest:
        assert item.get('category') in {'song','story','classical','white_noise','other'}
        path=source/item['file']; data=path.read_bytes()
        assert hashlib.sha256(data).hexdigest()==item['sha256']
        with wave.open(str(path),'rb') as w:
            assert (w.getnchannels(),w.getsampwidth(),w.getframerate())==(1,2,12000)
        if item['id'] in existing:
            assert existing[item['id']]['sha256']==item['sha256']
            print(item['id']+': already imported');continue
        meta=request('/admin/api/audio/files','PUT',data)
        assert meta['sha256']==item['sha256']
        track=request('/v1/audio/tracks','POST',{**meta,'id':item['id'],'title':item['title'],'category':item['category']})
        request('/admin/api/audio/'+track['id'],'PATCH',item['attribution'])
        print(item['id']+': imported and attributed')
    # Replacement is reversible and occurs only after every new track is verified.
    for item in manifest:
        for old in item.get('replaces',[]):
            assert old not in {r['id'] for r in manifest}
            if old in existing:
                request('/admin/api/audio/'+old,'PATCH',{'active':False})
                print(old+': disabled after replacement')
    print('Nursery library import: PASS')


if __name__=='__main__':main()
