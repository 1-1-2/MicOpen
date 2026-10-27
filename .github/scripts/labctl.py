#!/usr/bin/env python3
"""人和 Agent 共用的 dispatch、状态、连接、结束工具；凭据只从环境读取。
GitHub 2026-03-10 dispatch 返回精确 run ID；网络结果不确定时不自动重复提交。
"""
import argparse
import ipaddress
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import uuid
import zipfile
import io


def repo():
    value=os.environ.get('GH_REPO') or os.environ.get('GITHUB_REPOSITORY')
    if not value:
        url=subprocess.check_output(['git','remote','get-url','origin'],text=True).strip()
        m=re.search(r'github.com[:/]([^/]+/[^/]+?)(?:\.git)?$',url)
        value=m.group(1) if m else ''
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+',value or ''):raise ValueError('set GH_REPO=owner/repo')
    return value


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,*a):return None


def api(path,method='GET',data=None,raw=False):
    token=os.environ.get('GH_TOKEN') or os.environ.get('GITHUB_TOKEN')
    if not token:raise ValueError('GH_TOKEN is required (never pass it on the command line)')
    headers={'Authorization':'Bearer '+token,'Accept':'application/vnd.github+json','Content-Type':'application/json','User-Agent':'miclink-labctl','X-GitHub-Api-Version':'2026-03-10'}
    request=urllib.request.Request('https://api.github.com/repos/'+repo()+path,method=method,headers=headers,data=json.dumps(data).encode() if data is not None else None)
    try:
        with urllib.request.build_opener(NoRedirect()).open(request,timeout=30) as response:body=response.read()
    except urllib.error.HTTPError as e:
        if e.code not in [301,302,303,307,308]:
            try:detail=str(json.loads(e.read(8192)).get('message','')).replace(token,'[REDACTED]')
            except (ValueError,UnicodeError):detail=''
            raise ValueError(f'GitHub HTTP {e.code}: {method} {path}: {detail[:2000]}') from None
        # 下载签名地址时不把 GitHub token 转发给另一台主机。
        with urllib.request.urlopen(e.headers['Location'],timeout=30) as response:body=response.read()
    return body if raw else json.loads(body) if body else {}


def output(values):
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'],'a',encoding='utf-8') as f:
            for k,v in values.items():f.write(k+'='+str(v)+'\n')
    print(json.dumps(values,ensure_ascii=False))


def integer_input(value,name,minimum,maximum):
    if isinstance(value,bool):raise ValueError(name+' must be an integer')
    if isinstance(value,str):
        if not re.fullmatch(r'[0-9]+',value):raise ValueError(name+' must contain decimal digits only')
        value=int(value)
    if not isinstance(value,(int,float)) or value!=int(value) or not minimum<=value<=maximum:raise ValueError(name+' outside allowed integer range')
    return int(value)


def plan():
    a=json.loads(os.environ['LAB_INPUTS']);scenario=a['scenario'];target=a['target'];minutes=a['session_minutes'];source=a['artifact_run_id'];pause=a['pause_at']
    if scenario not in ['base','toolchain','built','installed','verify','dual'] or target not in ['windows','linux','android','both']:raise ValueError('invalid scenario/target')
    minutes=integer_input(minutes,'session_minutes',1,120)
    source=integer_input(source,'artifact_run_id',0,2**63-1)
    if scenario=='dual':
        target='both'
        if pause not in ['ready','after-test','on-failure','none']:raise ValueError('dual pause_at: ready/after-test/on-failure/none')
    elif target=='linux' and scenario not in ['base','toolchain']:raise ValueError('linux only supports base/toolchain; choose android for the emulator')
    elif target in ['android','both'] and pause in ['before-permissions','after-permissions']:raise ValueError('permission matrix pause points are Windows-only')
    if scenario not in ['verify','dual'] and pause not in ['ready','none']:raise ValueError('pause_at applies to verify/dual; preparation scenes use ready/none')
    products=['android','windows'] if target=='both' else [target]
    needs_product=scenario in ['built','installed','verify','dual']
    flags={p:str(needs_product and p in products and source==0).lower() for p in ['android','windows']}
    flags['dual']=str(scenario=='dual').lower()
    flags['matrix']=json.dumps({'include':[{'role':p,'os':'windows-latest' if p=='windows' else 'ubuntu-latest'} for p in products]},separators=(',',':'))
    source=int(source)
    if source:
        run=api(f'/actions/runs/{source}')
        if run.get('repository',{}).get('full_name',repo())!=repo():raise ValueError('artifact run belongs to a different repository')
        artifacts=api(f'/actions/runs/{source}/artifacts?per_page=100')['artifacts']
    for p in ['android','windows']:
        name=f'build-{p}-{os.environ.get("GITHUB_RUN_ID")}-{os.environ.get("GITHUB_RUN_ATTEMPT","1")}'
        if source and needs_product and p in products:
            candidates=[x for x in artifacts if re.fullmatch(f'build-{p}-{source}-[0-9]+',x['name']) and not x['expired']]
            if not candidates:raise ValueError('missing unexpired build artifact for '+p)
            name=max(candidates,key=lambda x:int(x['name'].rsplit('-',1)[1]))['name']
        flags['artifact_'+p]=name
    output(flags)
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'],'a',encoding='utf-8') as f:
            f.write(f'## Lab 执行计划\n\n场景 `{scenario}`；目标 `{target}`；暂停 `{pause}`；会话 {int(minutes)} 分钟；SSH 正文公开 `{a["stream_logs"]}`。\n\n产物来源：'+('本次构建' if not source else '指定运行 `'+str(source)+'`')+'。\n')


def connection(run,role,attempt,timeout):
    started=time.monotonic()
    if attempt is None:attempt=api(f'/actions/runs/{run}')['run_attempt']
    name=f'connection-{run}-{attempt}-{role}'
    while True:
        artifacts=api(f'/actions/runs/{run}/artifacts?per_page=100')['artifacts']
        matches=[a for a in artifacts if a['name']==name and not a['expired']]
        if matches:
            with zipfile.ZipFile(io.BytesIO(api('/actions/artifacts/'+str(matches[0]['id'])+'/zip',raw=True))) as z:
                if z.getinfo('connection.json').file_size>65536:raise ValueError('oversized connection metadata')
                c=json.loads(z.read('connection.json'))
            if str(c['run_id'])!=str(run) or str(c['attempt'])!=str(attempt) or c['role']!=role:raise ValueError('connection identity mismatch')
            if time.time()>=c['deadline_epoch']:raise ValueError('connection window has expired; start a new Lab')
            return c
        status=api(f'/actions/runs/{run}')
        if status['status']=='completed':raise ValueError('run ended before connection became ready')
        if time.monotonic()-started>=timeout:raise ValueError('connection readiness deadline reached')
        time.sleep(3)


def ssh_options(c,identity,known,relay_known,transport='auto'):
    address=ipaddress.ip_address(c['host'])
    if address.version!=4 or address not in ipaddress.ip_network('100.64.0.0/10'):raise ValueError('expected a Tailscale IPv4 address')
    if not re.fullmatch('[A-Za-z0-9_.-]+',c['user']):raise ValueError('invalid SSH user')
    if c['port']!=22:raise ValueError('unexpected SSH port')
    if not c['host_keys'] or any(not re.fullmatch(r'(?:ssh-ed25519|ssh-rsa|ecdsa-sha2-nistp(?:256|384|521)) [A-Za-z0-9+/]+=*(?: [^\r\n]*)?',k) for k in c['host_keys']):raise ValueError('invalid host public key')
    known.write_text(''.join(c['host']+' '+k+'\n' for k in c['host_keys']))
    cmd=['-F','none','-o','BatchMode=yes','-o','ConnectTimeout=25','-o','StrictHostKeyChecking=yes','-o','UserKnownHostsFile='+str(known)]
    if identity:cmd+=['-o','IdentitiesOnly=yes','-i',str(Path(identity).expanduser().resolve())]
    quote=subprocess.list2cmdline if os.name=='nt' else shlex.join
    use_relay=transport=='relay' or transport=='auto' and 'relay' in c
    if use_relay:
        from lab_relay import CA,VERSION
        r=c.get('relay',{})
        if r.get('kind')!='upterm' or r.get('host')!='uptermd.upterm.dev' or r.get('port')!=443 or r.get('ca')!=CA or r.get('version')!=VERSION:raise ValueError('unexpected relay metadata')
        if not re.fullmatch(r'[A-Za-z0-9_=:+/-]+',r.get('user','')) or '://' in r['user']:raise ValueError('invalid relay user')
        relay_known.write_text('@cert-authority [uptermd.upterm.dev]:443 '+CA+'\n')
        outer=['ssh','-F','none','-o','BatchMode=yes','-o','StrictHostKeyChecking=yes','-o','ConnectTimeout=20','-o','UserKnownHostsFile='+str(relay_known)]
        if identity:outer+=['-o','IdentitiesOnly=yes','-i',str(Path(identity).expanduser().resolve())]
        # 外层只转发字节。内层原生 OpenSSH 独立校验主机身份，支持退出码与 SFTP。
        outer+=['-o','ProxyCommand='+quote(['upterm','proxy','wss://'+r['user']+'@'+r['host']]),'-p','443','-l',r['user'],'-W','127.0.0.1:22',r['host']]
        cmd+=['-o','ProxyCommand='+quote(outer)]
    elif os.environ.get('TAILSCALE_SOCKET'):
        cmd+=['-o','ProxyCommand='+quote(['tailscale','--socket='+os.environ['TAILSCALE_SOCKET'],'nc','%h','%p'])]
    return cmd


def ssh(c,action,args,identity,transport='auto'):
    if time.time()>=c['deadline_epoch']:raise ValueError('connection window has expired')
    with tempfile.TemporaryDirectory(prefix='miclink-known-host-') as temp:
        cmd=['ssh']+ssh_options(c,identity,Path(temp)/'known_hosts',Path(temp)/'relay_hosts',transport)+[c['user']+'@'+c['host']]
        argv=[c['python'],c['script'],'--root',c['root'],action]
        if action=='exec':argv+=['--']+args
        command=subprocess.list2cmdline(argv) if c['role'] in ['windows','windows-client'] else shlex.join(argv)
        return subprocess.run(cmd+[command],check=False).returncode


def write_ssh_config(c,identity,destination,transport='auto'):
    destination=Path(destination).expanduser().resolve()
    if time.time()>=c['deadline_epoch']:raise ValueError('connection window has expired')
    # 将相同的主机校验和中继选项导出给原生 ssh/scp/sftp，不把私钥复制到配置。
    opts=ssh_options(c,identity,destination.with_name(destination.name+'.known_hosts'),destination.with_name(destination.name+'.relay_hosts'),transport)
    lines=['Host miclink-lab','  HostName '+c['host'],'  User '+c['user'],'  Port 22']
    i=0
    while i<len(opts):
        if opts[i]=='-F':i+=2;continue
        value=opts[i+1]
        if opts[i]=='-i':lines.append('  IdentityFile '+json.dumps(value))
        elif opts[i]=='-o':
            key,value=value.split('=',1)
            lines.append('  '+key+' '+(value if key=='ProxyCommand' else json.dumps(value)))
        i+=2
    destination.write_text('\n'.join(lines)+'\n');destination.chmod(0o600)
    print('SSH config saved: '+str(destination))


def dispatch(workflow,ref,items):
    inputs={}
    for item in items:
        k,v=item.split('=',1)
        # REST dispatch 的 inputs 走字符串；workflow 的类型声明负责还原布尔/数字。
        inputs[k]=v
    inputs.setdefault('request_id',uuid.uuid4().hex[:12])
    try:result=api('/actions/workflows/'+workflow+'/dispatches','POST',{'ref':ref,'inputs':inputs})
    except (urllib.error.URLError,TimeoutError):
        raise ValueError('dispatch response uncertain; inspect request_id='+inputs['request_id']+' before retrying') from None
    if not result.get('workflow_run_id'):raise ValueError('dispatch did not return run details; inspect request_id='+inputs['request_id'])
    print(json.dumps(result,ensure_ascii=False))


def barrier(phase):
    # 双机只在双方明确请求 ready 暂停时使用；故障留场不能等待一个已失败的对端。
    from lab_session import root_for,state,atomic
    import datetime as dt
    root=root_for();own=state(root);role=own['role']
    peer={'linux-app':'windows-client','windows-client':'linux-app'}[role]
    run=own['run_id'];attempt=own['attempt']
    name=f'readiness-{run}-{attempt}-{peer}' if phase=='ready' else f'session-{run}-{attempt}-{peer}'
    deadline=time.monotonic()+600 if phase=='ready' else time.monotonic()+max(0,own['deadline_epoch']-time.time())+180
    while time.monotonic()<deadline:
        arts=api(f'/actions/runs/{run}/artifacts?per_page=100')['artifacts']
        match=next((a for a in arts if a['name']==name and not a['expired']),None)
        if match:
            with zipfile.ZipFile(io.BytesIO(api('/actions/artifacts/'+str(match['id'])+'/zip',raw=True))) as z:
                data=json.loads(z.read('ready.json' if phase=='ready' else 'session.json'))
            if str(data['run_id'])!=str(run) or str(data['attempt'])!=str(attempt) or data['role']!=peer:raise ValueError('peer identity mismatch')
            if phase=='ready':
                parse=lambda text:dt.datetime.fromisoformat(text).timestamp()
                own['deadline_epoch']=max(parse(own['ready_at']),parse(data['ready_at']))+own['session_minutes']*60
                atomic(root/'state.json',own)
                connection_file=root/'connection/connection.json'
                info=json.loads(connection_file.read_text(encoding='utf-8'));info['deadline_epoch']=own['deadline_epoch'];atomic(connection_file,info)
            print('dual barrier passed: '+phase);return
        jobs=api(f'/actions/runs/{run}/jobs?per_page=100')['jobs']
        if any(j['name']==peer and j['status']=='completed' and j['conclusion']!='success' for j in jobs):raise ValueError('peer failed before '+phase+' barrier')
        print('waiting for peer '+phase,flush=True);time.sleep(5)
    raise ValueError('peer '+phase+' deadline reached')


def watch(run,timeout):
    deadline=time.monotonic()+timeout;previous=None
    while True:
        data=api(f'/actions/runs/{run}')
        jobs=api(f'/actions/runs/{run}/jobs?per_page=100')['jobs']
        snapshot=[{'id':j['id'],'job':j['name'],'state':j['status'],'result':j['conclusion'],'active_steps':[{'name':x['name'],'started_at':x['started_at']} for x in j.get('steps',[]) if x['status']=='in_progress']} for j in jobs]
        text=json.dumps(snapshot,ensure_ascii=False)
        if text!=previous:print(text,flush=True);previous=text
        if data['status']=='completed':return 0 if data['conclusion']=='success' else 1
        if time.monotonic()>=deadline:
            print('still running; observer deadline reached (run was not cancelled)',flush=True);return 75
        time.sleep(min(5,max(0,deadline-time.monotonic())))


def main():
    p=argparse.ArgumentParser(description=__doc__);s=p.add_subparsers(dest='cmd',required=True)
    s.add_parser('plan')
    a=s.add_parser('watch');a.add_argument('--run',type=int,required=True);a.add_argument('--timeout',type=int,default=60)
    a=s.add_parser('barrier');a.add_argument('--phase',choices=['ready','closed'],required=True)
    a=s.add_parser('dispatch');a.add_argument('workflow',choices=['ci.yml','lab.yml']);a.add_argument('--ref',default='main');a.add_argument('--input',action='append',default=[])
    for name in ['status','connection','exec','finish','ssh-config']:
        a=s.add_parser(name);a.add_argument('--run',type=int);a.add_argument('--connection-file');a.add_argument('--role',default='windows');a.add_argument('--attempt',type=int);a.add_argument('--timeout',type=int,default=600);a.add_argument('--identity',default=os.environ.get('SSH_IDENTITY'))
        a.add_argument('--transport',choices=['auto','tailnet','relay'],default='auto')
        if name in ['connection','ssh-config']:a.add_argument('--output')
        if name=='exec':a.add_argument('argv',nargs=argparse.REMAINDER)
    a=p.parse_args()
    if a.cmd=='plan':plan();return 0
    if a.cmd=='watch':return watch(a.run,a.timeout)
    if a.cmd=='barrier':barrier(a.phase);return 0
    if a.cmd=='dispatch':dispatch(a.workflow,a.ref,a.input);return 0
    if not a.run and not a.connection_file:raise ValueError('--run or --connection-file is required')
    if a.cmd=='status':
        if not a.run:raise ValueError('status requires --run')
        d=api(f'/actions/runs/{a.run}/jobs?per_page=100')
        print(json.dumps([{'job':j['name'],'status':j['status'],'result':j['conclusion'],'steps':[{'name':x['name'],'status':x['status'],'result':x['conclusion']} for x in j.get('steps',[]) if x['status']=='in_progress' or x['conclusion']=='failure']} for j in d['jobs']],ensure_ascii=False,indent=2));return 0
    c=json.loads(Path(a.connection_file).read_text(encoding='utf-8')) if a.connection_file else connection(a.run,a.role,a.attempt,a.timeout)
    if a.cmd=='connection':
        if a.output:
            p=Path(a.output);p.write_text(json.dumps(c,ensure_ascii=False,indent=2));p.chmod(0o600);print('Connection metadata saved: '+str(p))
        else:
            visible={k:c[k] for k in ['run_id','attempt','role','sha','host','user','port','deadline_epoch','stream_logs']}
            visible.update(transport='relay' if 'relay' in c else 'tailnet',hint='Use --output to save complete metadata without displaying relay connection material')
            print(json.dumps(visible,ensure_ascii=False,indent=2))
        return 0
    if a.cmd=='ssh-config':
        if not a.output:raise ValueError('ssh-config requires --output')
        write_ssh_config(c,a.identity,a.output,a.transport);return 0
    argv=a.argv[1:] if a.cmd=='exec' and a.argv[:1]==['--'] else a.argv if a.cmd=='exec' else []
    return ssh(c,a.cmd,argv,a.identity,a.transport)
if __name__=='__main__':
    try:sys.exit(main())
    except (ValueError,OSError,KeyError) as e:print('labctl: '+str(e),file=sys.stderr);sys.exit(2)
