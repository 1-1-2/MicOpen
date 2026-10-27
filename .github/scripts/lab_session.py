#!/usr/bin/env python3
"""Lab 会话控制与可选公开日志。只用标准库，Windows/Linux 共用。

历史 run 39642016060 中 tail -f | sed 的等待阶段没有转发任何命令，
308 行转录最后才被 cat。这里由前台监督进程按 SQLite 序号读取并 flush，
不依赖 sed 缓冲、PTY、后台 tail 或 SSH shell 的生命周期。
关闭 stream_logs 时不落命令或输出正文，不产生转录附件。
"""
import argparse
import codecs
import contextlib
import datetime as dt
import getpass
import json
import os
from pathlib import Path
import re
import shlex
import signal
import socket
import sqlite3
import subprocess
import sys
import threading
import time
import uuid


def now():
    return dt.datetime.now(dt.timezone.utc).isoformat()


def atomic(path, data):
    tmp = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
    tmp.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')
    os.replace(tmp, path)


def root_for(value=None):
    if value or os.environ.get('LAB_ROOT'):
        return Path(value or os.environ['LAB_ROOT']).resolve()
    temp = os.environ.get('RUNNER_TEMP')
    if not temp:
        raise ValueError('请用 --root 或 LAB_ROOT 指定当前会话目录')
    role = os.environ.get('LAB_ROLE', os.environ.get('RUNNER_OS', 'local').lower())
    if not re.fullmatch(r'[a-zA-Z0-9_-]+', role):
        raise ValueError('invalid role')
    return Path(temp) / ('miclink-lab-' + os.environ.get('GITHUB_RUN_ID', 'local') + '-' + os.environ.get('GITHUB_RUN_ATTEMPT', '1') + '-' + role)


def state(root):
    return json.loads((root / 'state.json').read_text(encoding='utf-8'))


@contextlib.contextmanager
def database(root):
    db = sqlite3.connect(root / 'events.sqlite', timeout=15)
    db.execute('CREATE TABLE IF NOT EXISTS events (seq INTEGER PRIMARY KEY, at TEXT, command TEXT, stream TEXT, text TEXT)')
    try:
        yield db
        db.commit()
    finally:
        db.close()


class Redactor:
    """按完整行脱敏；跨 read 分块先拼行。超长无换行内容整段抑制。

    这是补充保护，不是任意内容安全公开的证明；stream_logs 必须显式开启。
    PEM 起止可跨多行，不把整段私钥中间行放到公开输出。
    """
    def __init__(self):
        self.pem = False
        self.values = [v for k, v in os.environ.items() if len(v) >= 8 and (k in {'GH_TOKEN', 'GITHUB_TOKEN', 'TS_OAUTH_SECRET'} or k.endswith('_SECRET'))]

    def line(self, text):
        text = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', text).replace('\r', '')
        if self.pem:
            if re.search(r'-----END .*PRIVATE KEY-----', text):
                self.pem = False
            return '[PRIVATE KEY WITHHELD]'
        if re.search(r'-----BEGIN .*PRIVATE KEY-----', text):
            self.pem = not bool(re.search(r'-----END .*PRIVATE KEY-----', text))
            return '[PRIVATE KEY WITHHELD]'
        text = re.sub(r'(?:github_pat_[A-Za-z0-9_]+|gh[pousr]_[A-Za-z0-9_]+|tskey-[A-Za-z0-9_-]+)', '[TOKEN REDACTED]', text)
        text = re.sub(r'(?i)(Bearer\s+)[A-Za-z0-9._~+/-]+=*', r'\1[REDACTED]', text)
        for value in self.values:
            text = text.replace(value, '[SECRET REDACTED]')
        return ''.join(c for c in text if c == '\t' or ord(c) >= 32)


def record(root, command, stream, text):
    with database(root) as db:
        db.execute('INSERT INTO events(at,command,stream,text) VALUES(?,?,?,?)', (now(), command, stream, text))


def initialize(root, minutes, stream):
    if not 1 <= minutes <= 120:
        raise ValueError('session_minutes 必须在 1–120 之间')
    root.mkdir(parents=True, exist_ok=True)
    if (root / 'state.json').exists():
        raise ValueError('会话目录已存在；不能重置正在使用的现场')
    with database(root):
        pass
    info = dict(schema=1, run_id=os.environ.get('GITHUB_RUN_ID', 'local'), attempt=os.environ.get('GITHUB_RUN_ATTEMPT', '1'), role=os.environ.get('LAB_ROLE', 'local'), sha=os.environ.get('GITHUB_SHA', ''), phase='PREPARING', stream_logs=stream, session_minutes=minutes, created_at=now())
    atomic(root / 'state.json', info)
    # SSH 服务不会自动继承 job 新增的 PATH/SDK 变量；只保存这些明确允许的工具上下文。
    allowed = ['PATH','ANDROID_HOME','ANDROID_SDK_ROOT','JAVA_HOME','GITHUB_WORKSPACE','RUNNER_TEMP','CONTROL_PORT','MEDIA_PORT','REDIR_PORT','PYTHONUTF8']
    atomic(root / 'runtime.json', {'cwd':str(Path.cwd()),'env':{k:os.environ[k] for k in allowed if k in os.environ}})
    env = os.environ.get('GITHUB_ENV')
    if env:
        with open(env, 'a', encoding='utf-8') as f:
            f.write('LAB_ROOT=' + str(root) + '\n')
    return info


def ready(root, host=None):
    info = state(root)
    info.update(phase='READY', ready_at=now(), deadline_epoch=time.time() + info['session_minutes'] * 60)
    atomic(root / 'state.json', info)
    script = str(Path(__file__).resolve())
    public = {k: info[k] for k in ['schema', 'run_id', 'attempt', 'role', 'sha', 'stream_logs', 'ready_at', 'deadline_epoch']}
    public.update(host=host or socket.gethostname(), user=getpass.getuser(), port=22, workspace=str(Path.cwd()), script=script, root=str(root), python=sys.executable)
    keydir = Path(os.environ.get('ProgramData', 'C:/ProgramData')) / 'ssh' if os.name == 'nt' else Path('/etc/ssh')
    public['host_keys'] = [p.read_text().strip() for p in sorted(keydir.glob('ssh_host_*_key.pub'))]
    if (root / 'relay.json').exists():
        public['relay'] = json.loads((root / 'relay.json').read_text(encoding='utf-8'))
    readiness = root / 'readiness';readiness.mkdir(exist_ok=True)
    atomic(readiness / 'ready.json', {k:info[k] for k in ['run_id','attempt','role','ready_at','session_minutes']})
    out = root / 'connection';out.mkdir(exist_ok=True)
    atomic(out / 'connection.json', public)
    summary = os.environ.get('GITHUB_STEP_SUMMARY')
    if summary:
        with open(summary, 'a', encoding='utf-8') as f:
            f.write(f"\n连接方式：{'WSS 中继转原生 OpenSSH' if 'relay' in public else 'tailnet 普通 SSH'}。\n")
            f.write(f"\n### Lab 就绪：{public['role']}\n\nSSH：`ssh {public['user']}@{public['host']}`\n\n公开日志：{'开启' if info['stream_logs'] else '关闭'}；窗口 {info['session_minutes']} 分钟。\n\n连接附件包含状态/执行/结束所需路径和主机公钥；详见工作流 README。\n")
    return public


def wait_command(proc, root):
    # 已启动的命令也受窗口约束；不能只在启动前检查一次，否则会越过到期继续改现场。
    while True:
        try:return proc.wait(timeout=.2)
        except subprocess.TimeoutExpired:
            info=state(root)
            stop=json.loads((root/'stop.json').read_text()) if (root/'stop.json').exists() else {}
            finish=(root/'finish.json').exists() or stop.get('phase') in {'RELEASED','CANCELLED'}
            expired=bool(stop) or time.time()>=info['deadline_epoch'] or info['phase'] not in {'READY','WAITING'}
            if not finish and not expired:continue
            if os.name=='nt':
                subprocess.run(['taskkill','/PID',str(proc.pid),'/T','/F'],capture_output=True,timeout=15)
            else:
                try:os.killpg(proc.pid,signal.SIGKILL)
                except ProcessLookupError:pass
            proc.wait(timeout=15)
            return 130 if finish or info['phase']=='CANCELLED' else 124


def start_command(root,argv,**kwargs):
    proc=subprocess.Popen(argv,**kwargs)
    active=root/'active';active.mkdir(exist_ok=True)
    proc.lab_marker=active/(uuid.uuid4().hex+'.json')
    atomic(proc.lab_marker,{'pid':proc.pid,'started_at':now()})
    return proc


def execute(root, argv):
    info = state(root)
    if info['phase'] not in {'READY', 'WAITING'} or time.time() >= info['deadline_epoch'] or (root / 'finish.json').exists() or (root / 'stop.json').exists():
        raise ValueError('会话尚未就绪或已经结束')
    if not argv:
        raise ValueError('缺少命令；使用 exec -- <程序> <参数>')
    runtime = json.loads((root / 'runtime.json').read_text(encoding='utf-8'))
    environment = dict(os.environ);environment.update(runtime['env'])
    if argv[0] in ['python','python3']:argv = [sys.executable] + argv[1:]
    options={'start_new_session':True} if os.name!='nt' else {'creationflags':subprocess.CREATE_NEW_PROCESS_GROUP}
    if not info['stream_logs']:
        proc=start_command(root,argv,cwd=runtime['cwd'],env=environment,**options)
        try:return wait_command(proc,root)
        finally:proc.lab_marker.unlink(missing_ok=True)
    cid = uuid.uuid4().hex[:12]
    # 参数同样可能含秘密，逐行经过脱敏，不将原文落盘。
    red = Redactor()
    display = list(argv)
    # 内联脚本可能拼接秘密；日志保留程序和参数结构，不公开脚本正文。
    for i, arg in enumerate(display[:-1]):
        if arg.lower() in {'-c', '-command', '-encodedcommand', '-scriptblock'}:
            display[i + 1] = '[INLINE SCRIPT WITHHELD]'
    for line in shlex.join(display).splitlines():
        record(root, cid, 'command', red.line(line))
    proc = start_command(root,argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=runtime['cwd'], env=environment,**options)

    def pump(source, destination, stream):
        redactor = Redactor(); decoder = codecs.getincrementaldecoder('utf-8')('replace')
        pending = ''; dropping = False
        while True:
            chunk = os.read(source.fileno(), 4096)
            if not chunk:
                break
            # 原始输出只回 SSH 调用者；公开链路只读取脱敏数据库。
            if hasattr(destination, 'buffer'):
                destination.buffer.write(chunk);destination.buffer.flush()
            else:
                destination.write(chunk.decode('utf-8', 'replace'));destination.flush()
            pending += decoder.decode(chunk)
            while '\n' in pending:
                line, pending = pending.split('\n', 1)
                if dropping:
                    dropping = False
                elif len(line) > 16384:
                    record(root, cid, stream, '[OVERSIZED LINE WITHHELD]')
                else:
                    record(root, cid, stream, redactor.line(line))
            if len(pending) > 16384:
                pending = '';dropping = True
                record(root, cid, stream, '[OVERSIZED LINE WITHHELD]')
        pending += decoder.decode(b'', final=True)
        if pending and not dropping:
            record(root, cid, stream, redactor.line(pending))

    errors = []
    def guarded(*args):
        try:
            pump(*args)
        except Exception as e:
            errors.append(type(e).__name__)
    threads = [threading.Thread(target=guarded, args=(proc.stdout, sys.stdout, 'stdout')), threading.Thread(target=guarded, args=(proc.stderr, sys.stderr, 'stderr'))]
    for t in threads:t.start()
    rc = wait_command(proc,root)
    for t in threads:t.join()
    record(root, cid, 'exit', str(rc))
    proc.lab_marker.unlink(missing_ok=True)
    if errors:
        record(root, cid, 'error', 'capture failed: ' + ','.join(errors))
        return rc or 74
    return rc


def serve(root, seconds=None):
    info = state(root)
    deadline = info['deadline_epoch'] if seconds is None else min(info['deadline_epoch'], time.time() + seconds)
    info.update(phase='WAITING', supervisor_started_at=now());atomic(root / 'state.json', info)
    token = uuid.uuid4().hex
    # 用户输出不被 runner 当作 ::error::、::add-mask:: 等命令解释。
    print('::stop-commands::' + token, flush=True)
    cursor = 0; heartbeat = 0; interrupted = False; closing_started = None
    def stop(signum, frame):
        nonlocal interrupted
        interrupted = True
    old = {}
    for sig in [signal.SIGINT, signal.SIGTERM]:old[sig] = signal.signal(sig, stop)
    try:
        while True:
            if info['stream_logs']:
                with database(root) as db:
                    rows = db.execute('SELECT seq,at,command,stream,text FROM events WHERE seq>? ORDER BY seq LIMIT 256', (cursor,)).fetchall()
                for seq,at,cid,stream,text in rows:
                    print(f'[lab {seq} {at} {cid} {stream}] {text}', flush=True)
                    cursor = seq
            finished = (root / 'finish.json').exists()
            if finished or interrupted or time.time() >= deadline:
                # 先通知仍在执行的记录工具命令关闭，排空退出记录后再导出附件。
                if closing_started is None:
                    closing_started=time.monotonic()
                    atomic(root/'stop.json',{'phase':'RELEASED' if finished else 'CANCELLED' if interrupted else 'EXPIRED'})
                if list((root/'active').glob('*.json')):
                    if time.monotonic()-closing_started>20:
                        info.update(phase='CANCELLED',ended_at=now(),close_error='active command did not stop');atomic(root/'state.json',info)
                        print('[lab-state] active command cleanup failed',flush=True);return 1
                    time.sleep(.1);continue
                if info['stream_logs'] and rows:continue
                break
            if time.monotonic() >= heartbeat:
                print(f'[lab-state] role={info["role"]} phase=WAITING emitted_seq={cursor} remaining_seconds={max(0,int(deadline-time.time()))}', flush=True)
                heartbeat = time.monotonic() + 30
            time.sleep(.2)
        phase = 'RELEASED' if finished else 'CANCELLED' if interrupted else 'EXPIRED'
        info.update(phase=phase, ended_at=now(), emitted_seq=cursor);atomic(root / 'state.json', info)
        print('[lab-state] phase=' + phase, flush=True)
    finally:
        print('::' + token + '::', flush=True)
        for sig,handler in old.items():signal.signal(sig, handler)
    return 130 if interrupted else 0


def export(root):
    info = state(root);out=root / 'public-report';out.mkdir(exist_ok=True)
    atomic(out/'session.json', info)
    if info['stream_logs']:
        with database(root) as db:
            rows=db.execute('SELECT seq,at,command,stream,text FROM events ORDER BY seq').fetchall()
        with (out/'public-session.log').open('w',encoding='utf-8') as f:
            for seq,at,cid,stream,text in rows:f.write(f'[{seq} {at} {cid} {stream}] {text}\n')
    return str(out)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root')
    sub=parser.add_subparsers(dest='command',required=True)
    p=sub.add_parser('init');p.add_argument('--minutes',type=int,default=20);p.add_argument('--stream',choices=['true','false'],default='false')
    p=sub.add_parser('ready');p.add_argument('--host')
    p=sub.add_parser('serve');p.add_argument('--seconds',type=float)
    p=sub.add_parser('exec');p.add_argument('argv',nargs=argparse.REMAINDER)
    sub.add_parser('status');sub.add_parser('finish');sub.add_parser('export')
    a=parser.parse_args();root=root_for(a.root)
    if a.command=='init':print(json.dumps(initialize(root,a.minutes,a.stream=='true'),ensure_ascii=False))
    elif a.command=='ready':print(json.dumps(ready(root,a.host),ensure_ascii=False))
    elif a.command=='serve':return serve(root,a.seconds)
    elif a.command=='exec':return execute(root,a.argv[1:] if a.argv[:1]==['--'] else a.argv)
    elif a.command=='status':print(json.dumps(state(root),ensure_ascii=False))
    elif a.command=='finish':atomic(root/'finish.json',{'at':now()});print('finish requested')
    elif a.command=='export':print(export(root))
    return 0


if __name__=='__main__':
    try:sys.exit(main())
    except (ValueError,OSError,sqlite3.Error) as e:
        print(f'lab: {e}',file=sys.stderr);sys.exit(2)
