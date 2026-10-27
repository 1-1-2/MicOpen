#!/usr/bin/env python3
"""真实官方驱动夹具仅在验证 runner 落盘；不经过公开 artifact。
保留 NSIS -aou 重名语义、三架构 9 文件哈希及原生 inflate 9/9 与外部目录复制 18/18 检查。
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request

ROOT=Path(__file__).resolve().parents[3]
HASHES=json.loads(Path(__file__).with_name('driver-hashes.json').read_text())

def main():
    work=Path(os.environ['RUNNER_TEMP'])/'miclink-driver-fixture';work.mkdir(exist_ok=True)
    installer=work/'WOMicClientSetup6.3.exe'
    if not installer.exists():
        with urllib.request.urlopen('https://wolicheng.com/womic/downloads/WOMicClientSetup6.3.exe',timeout=90) as r:installer.write_bytes(r.read())
    if hashlib.sha256(installer.read_bytes()).hexdigest()!=HASHES[installer.name]:raise ValueError('installer hash mismatch')
    seven=shutil.which('7z') or str(Path(os.environ.get('ProgramFiles','C:/Program Files'))/'7-Zip/7z.exe')
    extracted=work/'extracted'
    if extracted.exists():shutil.rmtree(extracted)
    extracted.mkdir()
    subprocess.run([seven,'x','-aou','-y',str(installer),'-o'+str(extracted)],check=True,stdout=subprocess.DEVNULL,timeout=120)
    drivers=extracted/'driver'
    for name,h in HASHES.items():
        if name==installer.name:continue
        p=drivers/name
        if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest()!=h:raise ValueError('driver hash mismatch: '+name)
    # 真实夹具生成到被忽略的文件，本地编译原有 C 解压自检，之后绝不上传它。
    subprocess.run([sys.executable,'tools/pack_driver.py','--driver-dir',str(drivers)],cwd=ROOT/'client',check=True,timeout=60)
    compiler=shutil.which('gcc')
    if not compiler:
        candidates=list(Path('C:/mingw64/bin').glob('gcc.exe'))+list(Path('C:/ProgramData/chocolatey/lib/mingw').glob('**/gcc.exe'))
        compiler=str(candidates[0]) if candidates else None
    if not compiler:
        subprocess.run(['choco','install','mingw','-y','--no-progress','--execution-timeout=240'],check=True,timeout=300)
        candidates=list(Path('C:/ProgramData/chocolatey/lib/mingw').glob('**/gcc.exe'))+list(Path('C:/mingw64/bin').glob('gcc.exe'))
        if not candidates:raise ValueError('native test compiler unavailable')
        compiler=str(candidates[0])
    dest=ROOT/'dist';dest.mkdir(exist_ok=True)
    native=dest/'test_drvpkg.exe'
    env=dict(os.environ);env['PATH']=str(Path(compiler).parent)+os.pathsep+env['PATH']
    subprocess.run([compiler,'-Os','-std=c99','-D_WIN32_WINNT=0x0A00','-DMICLINK_DRVPKG_EMBED_ARRAY=1','-o',str(native),'tools/test_drvpkg.c','src/drvpkg.c','-lkernel32','-luser32','-lshell32','-lole32'],cwd=ROOT/'client',env=env,check=True,timeout=120)
    out=subprocess.run([str(native)],capture_output=True,check=False,timeout=60)
    text=out.stdout.decode('utf-8','replace')
    if out.returncode or text.count('ok  :')!=9 or text.count('external copy:')!=6 or 'RESULT=PASS' not in text:
        print(text);raise ValueError('native driver inflate/external copy self-test failed')
    print('native driver fixture hash/inflate: 9/9 PASS; external directory copy: 18/18 PASS')
    # 接收端是 amd64，外部安装包需使用 amd64 三件套，而非 NSIS 第一套 x86。
    target=dest/'womic-driver';target.mkdir(exist_ok=True)
    for suffix in ['inf','sys','cat']:shutil.copyfile(drivers/('womic_1.'+suffix),target/('womic.'+suffix))

if __name__=='__main__':main()
