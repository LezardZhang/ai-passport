"""Upgrade only the deployed sidecar, checkpoint both DBs and retain rollback."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import sqlite3
import subprocess
import tarfile
import time

TARGET='cockpit-cloud-cloud-backup-1'


def run(args,capture=False):
    return subprocess.run(args,check=True,text=True,stdout=subprocess.PIPE if capture else None).stdout


def remote(code): return run(['docker','exec',TARGET,'python','-c',code],True)


def ready():
    for _ in range(60):
        r=subprocess.run(['docker','exec',TARGET,'python','-c',"import urllib.request; urllib.request.urlopen('http://127.0.0.1:8080/readyz',timeout=2).read()"],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        if not r.returncode:return
        time.sleep(1)
    raise RuntimeError('readiness timeout')


def main():
    os.umask(0o077)
    release=Path(__file__).resolve().parents[1]; state=release/'deployment-state';state.mkdir(exist_ok=False)
    before_containers=run(['docker','ps','-a','--format','{{.Names}} {{.ID}}'],True)
    (state/'containers-before.txt').write_text(before_containers)
    info=json.loads(run(['docker','inspect',TARGET],True))[0];labels=info['Config']['Labels']
    root=Path(labels['com.docker.compose.project.working_dir'])
    if root!=Path('/home/chatgpt/cockpit-cloud-fresh') or labels['com.docker.compose.service']!='cloud-backup':raise RuntimeError('unexpected deployment target')
    env=dict(s.split('=',1) for s in info['Config']['Env'] if '=' in s)
    mounts={m['Destination']:m['Source'] for m in info['Mounts']}
    config,data=Path(mounts['/config']),Path(mounts['/data'])
    child_db=config/'childcare.db'
    if not child_db.is_file():raise RuntimeError('expected existing childcare DB')
    (state/'original-inspection.json').write_text(json.dumps(info))
    # Online SQLite backup preserves the pre-upgrade childcare schema and all records.
    source=sqlite3.connect('file:'+str(child_db)+'?mode=ro',uri=True);dest=sqlite3.connect(state/'childcare-before.db')
    source.backup(dest);dest.close();source.close()
    stamp=time.strftime('%Y%m%d-%H%M%S',time.gmtime())
    old=info['Image']; base='personal-services-base:'+stamp; image='personal-services:'+stamp
    run(['docker','tag',old,base])
    shared=env['CLOUDBACKUP_ADMIN_TOKEN']
    child_env={k:v for k,v in env.items() if k.startswith('CHILDCARE_')}
    child_env.update(CHILDCARE_ADMIN_TOKEN=shared,CHILDCARE_ADMIN_PASSWORD=shared)
    for required in ('CHILDCARE_DEVICE_TOKEN','CHILDCARE_HERMES_TOKEN','CHILDCARE_PUBLIC_READ_TOKEN','CHILDCARE_SESSION_SECRET','CHILDCARE_PUBLIC_BASE_URL'):
        if not child_env.get(required):raise RuntimeError('existing role configuration missing')
    private=release/'childcare.env';private.write_text(''.join(k+'='+v+'\n' for k,v in child_env.items()))
    override=release/'installed.override.json'
    candidate_settings={'services':{'cloud-backup':{'image':image,'build':{'context':str(release),'dockerfile':'deploy/Dockerfile.installed','args':{'BACKUP_IMAGE':base}},'env_file':[str(private)],'environment':{'PERSONAL_SERVICES_VERIFY_ONLY':'1'}}}}
    override.write_text(json.dumps(candidate_settings))
    compose=['docker','compose','--project-name',labels['com.docker.compose.project'],'--project-directory',str(root)]
    for name in ('.env','.env.official-backend-v25','.env.cloud-backup'):
        file=root/name
        if file.is_file():compose+=['--env-file',str(file)];shutil.copy2(file,state/name)
    for i,file in enumerate(labels['com.docker.compose.project.config_files'].split(',')):
        compose+=['-f',file];shutil.copy2(file,state/(str(i)+'-'+Path(file).name))
    candidate=compose+['-f',str(override)]
    rollback=release/'rollback.override.json';rollback.write_text(json.dumps({'services':{'cloud-backup':{'image':old}}}))
    rollback_cmd=compose+['-f',str(rollback),'up','-d','--no-deps','--no-build','cloud-backup']
    (release/'rollback.sh').write_text('#!/bin/sh\nset -eu\n'+shlex.join(rollback_cmd)+'\n')
    (release/'service-compose.sh').write_text('#!/bin/sh\nset -eu\n'+shlex.join(candidate)+' "$@"\n')
    run(candidate+['config','--quiet']);run(candidate+['build','cloud-backup'])
    print('Candidate image: PASS',flush=True)
    meta=json.loads(remote("import os,urllib.request; r=urllib.request.Request('http://127.0.0.1:8080/admin/v1/backups',data=b'',headers={'Authorization':'Bearer '+os.environ['CLOUDBACKUP_ADMIN_TOKEN']},method='POST'); print(urllib.request.urlopen(r,timeout=900).read().decode())"))
    archive=data/'backups'/(meta['id']+'.tar.gz')
    h=hashlib.sha256()
    with archive.open('rb') as stream:
        for block in iter(lambda:stream.read(1048576),b''):h.update(block)
    if h.hexdigest()!=meta['sha256'] or archive.stat().st_size!=meta['size_bytes']:raise RuntimeError('archive verification failed')
    # Original backup creation already checks every object's SHA256; verify the actual compressed copy too.
    with tarfile.open(archive,'r:gz') as tar:
        manifest=json.load(tar.extractfile('manifest.json'))
        for obj in manifest['objects']:
            h=hashlib.sha256();size=0;member=tar.extractfile('data/'+obj['key'])
            for block in iter(lambda:member.read(1048576),b''):h.update(block);size+=len(block)
            if h.hexdigest()!=obj['sha256'] or size!=obj['size']:raise RuntimeError('archived object mismatch')
    (state/'backup.json').write_text(json.dumps(meta));print('Both database checkpoints and full backup: PASS',flush=True)
    spec=importlib.util.spec_from_file_location('inventory',release/'deploy/check_existing_data.py');inv=importlib.util.module_from_spec(spec);spec.loader.exec_module(inv)
    stopped=False
    try:
        run(['docker','stop',TARGET]);stopped=True
        before=inv.inventory(config/'cloud-backup.db',data);(state/'before.json').write_text(json.dumps(before))
        run(candidate+['up','-d','--no-deps','--no-build','cloud-backup']);ready()
        after=inv.inventory(config/'cloud-backup.db',data)
        if before!=after:raise RuntimeError('original backup data differs')
        (state/'after.json').write_text(json.dumps(after))
        remote("import os,urllib.request,json,io,zipfile; b='http://127.0.0.1:8080'; t=os.environ['CLOUDBACKUP_ADMIN_TOKEN']; r=urllib.request.Request(b+'/childcare/admin/api/personal',headers={'Authorization':'Bearer '+t}); assert json.loads(urllib.request.urlopen(r).read())['child']; r=urllib.request.Request(b+'/childcare/admin/api/skills/hermes',headers={'Authorization':'Bearer '+t}); z=zipfile.ZipFile(io.BytesIO(urllib.request.urlopen(r).read())); assert 'childcare-hermes/connection.json' in z.namelist()")
        candidate_settings['services']['cloud-backup']['environment']['PERSONAL_SERVICES_VERIFY_ONLY']='0';override.write_text(json.dumps(candidate_settings))
        run(candidate+['up','-d','--no-deps','--no-build','cloud-backup']);ready()
        before_ids=dict(s.split() for s in before_containers.splitlines());after_ids=dict(s.split() for s in run(['docker','ps','-a','--format','{{.Names}} {{.ID}}'],True).splitlines())
        assert [n for n in before_ids if before_ids[n]!=after_ids.get(n)]==[TARGET]
        (state/'result.json').write_text(json.dumps({'status':'installed','image':image,'old_image':old,'backup':meta,'counts':before['counts']},indent=2))
        print('UPGRADE PASS; original data preserved, other containers unchanged',flush=True)
    except Exception:
        if stopped:run(rollback_cmd);ready();print('Original service restored',flush=True)
        raise


if __name__=='__main__':main()
