#!/usr/bin/env python3
"""CI/Lab 共用的有界进程、公开产物准备、来源核验与汇总。"""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import struct
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(Path(__file__).parent))
from lab_session import Redactor


def run(seconds, argv):
    if argv[:1]==['--']:argv=argv[1:]
    if not argv:return 2
    kwargs={'start_new_session':True} if os.name!='nt' else {'creationflags':subprocess.CREATE_NEW_PROCESS_GROUP}
    p=subprocess.Popen(argv,**kwargs)
    try:return p.wait(timeout=seconds)
    except (subprocess.TimeoutExpired,KeyboardInterrupt):
        if os.name=='nt':subprocess.run(['taskkill','/PID',str(p.pid),'/T','/F'],capture_output=True)
        else:os.killpg(p.pid,signal.SIGKILL)
        p.wait();print('process deadline reached',file=sys.stderr);return 124


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def safe_exe(path):
    # count>0 的 MICLDRV1 包可藏在 .rsrc 或测试数组中，不能仅排除 .sys。
    b=path.read_bytes()
    for m in re.finditer(b'MICLDRV1',b):
        if m.start()+16<=len(b):
            version,count=struct.unpack_from('<II',b,m.start()+8)
            if version==1 and count>0:raise ValueError('embedded driver data: '+path.name)


def stage(kind):
    out=ROOT/'_public'/kind
    if out.exists():shutil.rmtree(out)
    out.mkdir(parents=True)
    files=[]
    if kind=='windows':
        # 强制空包的构建门禁，与接收端现场获取的官方夹具完全分离。
        if (ROOT/'client/res/drvpkg.bin').read_bytes()!=b'MICLDRV1'+struct.pack('<II',1,0):raise ValueError('public installer must use --empty')
        files=[ROOT/'client/build/womic.exe',ROOT/'client/build/womic-drvinst.exe']
        for p in files:safe_exe(p)
    elif kind=='android':files=list((ROOT/'android/app/build/outputs/apk/debug').glob('*.apk'))
    else:
        # 不上传整个 probe-results。人工 Lab 只导出结构化摘要；不会自动打包桌面或音频。
        base=ROOT/'probe-results'
        patterns=['verdict.txt','relay-stats.json','record/summary.json']
        if kind=='report':patterns+=['logs/[a-h]*.txt','logs/0-env.txt','logs/hello*.txt','interop/attempt-*.out','ui-screenshots/*.png','ui-screenshots/*.xml','screenshots/*.png']
        for pattern in patterns:files.extend(base.glob(pattern))
        files=sorted(set(p for p in files if p.is_file() and not p.is_symlink()))
    if kind in ['windows','android'] and not files:raise ValueError('missing build product')
    license_names={}
    if kind in ['windows','android']:
        opus=ROOT/('client/deps/opus/COPYING' if kind=='windows' else 'android/app/src/main/cpp/opus/COPYING')
        files += [ROOT/'LICENSE',ROOT/'NOTICE.md',opus]
        license_names[str(opus)]='Opus-COPYING.txt'
    entries=[]
    for p in files:
        if not p.is_file() or p.is_symlink():raise ValueError('invalid source: '+str(p))
        if p.stat().st_size>32*1024*1024:raise ValueError('artifact file exceeds 32 MiB')
        rel=license_names.get(str(p),p.name) if kind in ['windows','android'] else p.relative_to(ROOT/'probe-results')
        dest=out/rel;dest.parent.mkdir(parents=True,exist_ok=True)
        if p.suffix.lower() in ['.txt','.log','.out','.json','.xml']:
            r=Redactor();text='\n'.join(r.line(x) for x in p.read_text(encoding='utf-8',errors='replace').splitlines())+'\n'
            text=text.replace(str(ROOT),'<workspace>').replace(str(ROOT).replace('\\','/'),'<workspace>')
            if 'PRIVATE KEY-----' in text or re.search(r'github_pat_[A-Za-z0-9_]{20,}|tskey-[A-Za-z0-9_-]{15,}',text):raise ValueError('secret gate: '+str(rel))
            dest.write_text(text,encoding='utf-8')
        else:shutil.copyfile(p,dest)
        entries.append({'path':str(rel).replace('\\','/'),'bytes':dest.stat().st_size,'sha256':sha(dest)})
    subprocess_sha=subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip()
    manifest=dict(schema=1,kind=kind,sha=subprocess_sha,run_id=os.environ.get('GITHUB_RUN_ID','local'),attempt=os.environ.get('GITHUB_RUN_ATTEMPT','1'),embedded_drivers=False,files=entries)
    (out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (out/'SHA256SUMS.txt').write_text(''.join(f'{e["sha256"]}  {e["path"]}\n' for e in entries),encoding='utf-8')
    print(f'public artifact staged: {kind}, {len(entries)} files')


def verify(path,expected=None):
    path=Path(path);m=json.loads((path/'manifest.json').read_text())
    if expected and m['sha']!=expected:raise ValueError('artifact source SHA differs from requested source')
    if m.get('embedded_drivers') is not False:raise ValueError('unexpected embedded driver product')
    for e in m['files']:
        p=(path/e['path']).resolve()
        if not p.is_relative_to(path.resolve()) or not p.is_file() or p.is_symlink():raise ValueError('invalid artifact path')
        if sha(p)!=e['sha256']:raise ValueError('artifact digest mismatch')
        if p.suffix=='.exe':safe_exe(p)
    print('artifact verified: '+m['sha'])
    summary=os.environ.get('GITHUB_STEP_SUMMARY')
    if summary:
        with open(summary,'a',encoding='utf-8') as f:f.write('\nArtifact source SHA: `'+m['sha']+'`\n')


def plan(task,target):
    if task not in ['build','verify'] or target not in ['auto','android','windows','both']:raise ValueError('invalid CI input')
    event=os.environ.get('GITHUB_EVENT_NAME','workflow_dispatch')
    android=windows=True
    if target!='auto':android=target in ['android','both'];windows=target in ['windows','both']
    elif event in ['push','pull_request']:
        payload=json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text())
        base=payload.get('before') if event=='push' else payload['pull_request']['base']['sha']
        try:
            files=subprocess.check_output(['git','diff','--name-only',base,'HEAD'],cwd=ROOT,text=True).splitlines()
            shared=any(p.startswith(('.github/','tools/')) for p in files)
            android=shared or any(p.startswith('android/') for p in files)
            windows=shared or any(p.startswith('client/') for p in files)
        except subprocess.CalledProcessError:android=windows=True
    flags={'android':str(android).lower(),'windows':str(windows).lower(),'verify':str(task=='verify').lower()}
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'],'a') as f:
            for k,v in flags.items():f.write(f'{k}={v}\n')
    print(json.dumps(flags))


def summary():
    jobs=json.loads(os.environ.get('NEEDS_JSON','{}'));failed=[]
    # 汇总必须区分“未选中”和“依赖失败后被跳过”，不能让 skipped 洗绿。
    plan_outputs=jobs.get('plan',{}).get('outputs',{})
    required={'plan'}
    if 'checks' in jobs:required.add('checks')
    if 'scene' in jobs:
        required.update(['linux-app','windows-client'] if plan_outputs.get('dual')=='true' else ['scene'])
    if plan_outputs.get('android')=='true':required.add('build-android')
    if plan_outputs.get('windows')=='true':required.add('build-windows')
    if plan_outputs.get('verify')=='true':
        if 'build-android' in required:required.update(['protocol-tests','verify-android'])
        if 'build-windows' in required:required.add('verify-windows')
    failed.extend(sorted(required-set(jobs)))
    for name,j in jobs.items():
        print(name,j.get('result'))
        if j.get('result') in ['failure','cancelled'] or (name in required and j.get('result')!='success'):failed.append(name)
    return bool(failed)


def main():
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='cmd',required=True)
    a=sub.add_parser('run');a.add_argument('--seconds',type=int,required=True);a.add_argument('argv',nargs=argparse.REMAINDER)
    a=sub.add_parser('stage');a.add_argument('--kind',choices=['windows','android','report','lab-report'],required=True)
    a=sub.add_parser('verify');a.add_argument('path');a.add_argument('--sha')
    a=sub.add_parser('plan');a.add_argument('--task',default='verify');a.add_argument('--target',default='auto')
    sub.add_parser('summary');a=p.parse_args()
    if a.cmd=='run':return run(a.seconds,a.argv)
    if a.cmd=='stage':stage(a.kind)
    if a.cmd=='verify':verify(a.path,a.sha)
    if a.cmd=='plan':plan(a.task,a.target)
    if a.cmd=='summary':return summary()
    return 0
if __name__=='__main__':
    try:sys.exit(main())
    except (ValueError,OSError) as e:print('ci: '+str(e),file=sys.stderr);sys.exit(2)
