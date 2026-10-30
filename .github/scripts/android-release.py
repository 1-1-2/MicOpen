#!/usr/bin/env python3
"""核对双 APK、R8 映射、非调试 release 与原生 ABI；不处理分发私钥。"""
import json
import os
from pathlib import Path
import re
import subprocess
import zipfile

ROOT=Path(__file__).resolve().parents[2]

def inspect(apk,tools):
    badging=subprocess.check_output([str(tools/'aapt'),'dump','badging',str(apk)],text=True)
    cert=subprocess.check_output([str(tools/'apksigner'),'verify','--verbose','--print-certs',str(apk)],text=True)
    manifest=subprocess.check_output([str(tools/'aapt'),'dump','xmltree',str(apk),'AndroidManifest.xml'],text=True)
    with zipfile.ZipFile(apk) as z:
        dex=sum(i.file_size for i in z.infolist() if i.filename.endswith('.dex'))
        abis=sorted({n.split('/')[1] for n in z.namelist() if n.startswith('lib/') and n.endswith('/libmiclink_opus.so')})
    fingerprint=re.search(r'Signer #1 certificate SHA-256 digest: ([0-9a-f]+)',cert)
    if not fingerprint:raise ValueError('missing verified APK signer')
    return dict(direct_probe='DirectProbeActivity' in manifest,bytes=apk.stat().st_size,dex_bytes=dex,debuggable='application-debuggable' in badging,abis=abis,certificate_sha256=fingerprint[1])

def main():
    tools=Path(os.environ.get('ANDROID_HOME') or os.environ['ANDROID_SDK_ROOT'])/'build-tools/36.0.0'
    outputs=ROOT/'android/app/build/outputs'
    variants={v:inspect(outputs/f'apk/{v}/app-{v}.apk',tools) for v in ['debug','release']}
    if not variants['debug']['debuggable'] or variants['release']['debuggable']:
        raise ValueError('APK debuggable flag does not match its variant')
    if variants['release']['direct_probe'] or not variants['debug']['direct_probe']:
        raise ValueError('Direct probe must exist only in debug APK')
    expected=['arm64-v8a','armeabi-v7a','x86_64']
    if any(v['abis']!=expected for v in variants.values()):raise ValueError('incomplete Android ABI set')
    mapping=outputs/'mapping/release/mapping.txt'
    text=mapping.read_text()
    if not any(a!=b for a,b in re.findall(r'^([\w.$]+) -> ([\w.$]+):$',text,re.M)):
        raise ValueError('R8 mapping does not show optimized/renamed classes')
    config=(ROOT/'android/app/build.gradle.kts').read_text()
    if 'isMinifyEnabled = true' not in config or 'isShrinkResources = true' not in config:
        raise ValueError('release optimization configuration was disabled')
    if variants['release']['dex_bytes']>=variants['debug']['dex_bytes']:
        raise ValueError('release DEX was not reduced')
    report=dict(schema=1,variants=variants,minify=True,shrink_resources=True,signing='CI test key; distribution is signed locally',mapping_bytes=mapping.stat().st_size)
    (outputs/'release-info.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(report,ensure_ascii=False,indent=2))

if __name__=='__main__':main()
