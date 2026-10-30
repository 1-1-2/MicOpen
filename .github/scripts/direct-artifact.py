#!/usr/bin/env python3
"""Reuse an APK only when its build passed and all application inputs are unchanged."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import urllib.request
import ci

PRODUCT_INPUTS = ['android', 'client', '.github/actions/build-android', '.github/actions/build-direct',
                  '.github/actions/build-windows', '.github/actions/android-tools',
                  '.github/scripts/android-release.py', '.github/scripts/fetch-opus.sh',
                  '.github/scripts/setup-android-sdk.sh']


def numeric_run(value):
    if not re.fullmatch(r'[0-9]+', value): raise ValueError('invalid artifact run ID')
    return int(value)


def verify_reuse(folder, run):
    meta = json.loads((folder/'manifest.json').read_text())
    sha = meta['sha']
    if not re.fullmatch(r'[0-9a-f]{40}', sha): raise ValueError('invalid source SHA')
    if meta['run_id'] != str(run) or meta['kind'] != 'android': raise ValueError('artifact origin mismatch')
    ci.verify(folder, sha)
    changed = subprocess.check_output(['git', 'diff', '--name-only', sha, 'HEAD', '--']+PRODUCT_INPUTS, text=True)
    if changed.strip(): raise ValueError('application inputs changed; rebuild APK instead of reusing')
    repo = os.environ['GITHUB_REPOSITORY']; token = os.environ['GH_TOKEN']
    req = urllib.request.Request(f'https://api.github.com/repos/{repo}/actions/runs/{run}/jobs?per_page=100',
        headers={'Authorization': 'Bearer '+token, 'User-Agent': 'MicOpen-direct-provenance'})
    with urllib.request.urlopen(req, timeout=30) as r: jobs = json.load(r)['jobs']
    if not any(j['name']=='build-android' and j['conclusion']=='success' for j in jobs):
        raise ValueError('source Android build did not pass')
    print('Verified unchanged application inputs; APK source='+sha+'; harness='+os.environ['GITHUB_SHA'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('mode', choices=['plan', 'verify'])
    p.add_argument('--run', default='0')
    p.add_argument('--api', default='none')
    p.add_argument('--folder', type=Path, default=Path('apk'))
    a = p.parse_args(); run = numeric_run(a.run)
    if a.mode == 'plan':
        if not run or a.api not in ['35','37']: raise ValueError('reuse needs a run ID and an emulator API')
        with open(os.environ['GITHUB_OUTPUT'], 'a') as f: f.write('android=false\nwindows=false\nverify=false\n')
        print('Harness-only retest; application build will be reused after provenance verification')
    elif run: verify_reuse(a.folder, run)
    else: ci.verify(a.folder, os.environ['GITHUB_SHA'])


if __name__ == '__main__': main()
