#!/usr/bin/env python3
"""可选 WSS 中继：转发至原生 OpenSSH，私钥只留在操作端。

默认仍使用 tailnet。仅显式选择 relay 时启动公共中继；固定版本、哈希和 CA。
Upterm 的原始 PTY 不作为 Agent 命令通道；内层 OpenSSH 保持退出码与文件传输语义。"""
import hashlib,io,json,os,re,subprocess,sys,tarfile,urllib.request
from pathlib import Path
SCRIPTS=Path(__file__).resolve().parent
sys.path.insert(0,str(SCRIPTS))
import lab_session
HASHES={'upterm_linux_amd64.tar.gz': '065c172d7a0599a376ce0763a135792eee7e3c191ae78cfb335a07e900525231', 'upterm_windows_amd64.tar.gz': 'f5ced4f6587675ae85ce3e17aa13a8a4b4c35a314ee02f847ca2e4f7e1ee8c4e'}
VERSION='0.40.0'
CA='ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAICiecex8Dq718eSe1CCLgLvDmI7AagvCtax7brPFWkh4'

def relay_user(command):
    # 从完整 WSS URL 取 user，避免把 wss:// 一起误当 SSH 用户。
    m=re.search(r'wss://([A-Za-z0-9_=:+/-]+)@uptermd[.]upterm[.]dev(?=[:/\s\'\"]|$)',command)
    if not m:raise ValueError('unexpected relay connection metadata')
    return m.group(1)


def start():
    root=lab_session.root_for();work=root/'relay';work.mkdir(exist_ok=True)
    suffix='windows_amd64' if os.name=='nt' else 'linux_amd64'
    archive='upterm_'+suffix+'.tar.gz'
    with urllib.request.urlopen(f'https://github.com/owenthereal/upterm/releases/download/v{VERSION}/{archive}',timeout=60) as r:b=r.read()
    if hashlib.sha256(b).hexdigest()!=HASHES[archive]:raise ValueError('relay binary hash mismatch')
    exe=work/('upterm.exe' if os.name=='nt' else 'upterm')
    with tarfile.open(fileobj=io.BytesIO(b),mode='r:gz') as t:
        member=next(m for m in t.getmembers() if Path(m.name).name==exe.name)
        exe.write_bytes(t.extractfile(member).read())
    exe.chmod(0o700)
    public=work/'authorized_keys';public.write_text(os.environ['DEBUG_PUBKEY'].strip()+'\n')
    known=work/'known_hosts';known.write_text('@cert-authority uptermd.upterm.dev '+CA+'\n@cert-authority [uptermd.upterm.dev]:443 '+CA+'\n')
    name='miclink-'+os.environ['LAB_ROLE']
    cmd=[str(exe),'host','--detach','--accept','--name',name,'--server','wss://uptermd.upterm.dev','--authorized-keys',str(public),'--known-hosts',str(known),'--allow-local-tcp-forwarding','--no-sftp','-o','json','--',sys.executable,'-c','import time;time.sleep(9000)']
    result=subprocess.run(cmd,capture_output=True,text=True,timeout=120)
    if result.returncode:raise RuntimeError(result.stderr[-4000:])
    info=json.loads(result.stdout);(work/'host.json').write_text(json.dumps(info))
    user=relay_user(info['sshCommand'])
    lab_session.atomic(root/'relay.json',{'kind':'upterm','version':VERSION,'host':'uptermd.upterm.dev','port':443,'user':user,'ca':CA})
    print('Native SSH over WSS relay ready; specified public key required.')

def stop():
    root=lab_session.root_for();exe=root/'relay'/('upterm.exe' if os.name=='nt' else 'upterm')
    if exe.exists():
        result=subprocess.run([str(exe),'session','stop','miclink-'+os.environ['LAB_ROLE']],timeout=30,check=False)
        if result.returncode not in [0,4]:raise RuntimeError('relay cleanup failed')

if __name__=='__main__':{'start':start,'stop':stop}[sys.argv[1]]()
