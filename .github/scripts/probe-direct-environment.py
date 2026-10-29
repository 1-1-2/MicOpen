#!/usr/bin/env python3
"""Read-only inventory, not a Wi-Fi Direct pairing success test."""
import json
import os
from pathlib import Path
import platform
import re
import subprocess


def command(args):
    try:
        r = subprocess.run(args, capture_output=True, text=True, errors='replace', timeout=25)
        return {'exit_code': r.returncode, 'stdout': r.stdout.strip(), 'stderr': r.stderr.strip()}
    except (OSError, subprocess.TimeoutExpired) as e:
        return {'error': type(e).__name__}


report = {'kind': 'capability_inventory_only', 'os': platform.system(), 'pairing_tested': False}
if platform.system() == 'Windows':
    result = command(['pwsh', '-NoProfile', '-NonInteractive', '-Command',
        '$ErrorActionPreference="Stop"; @(Get-NetAdapter -IncludeHidden | '
        'Select-Object InterfaceDescription,InterfaceType,NdisPhysicalMedium,Status) | '
        'ConvertTo-Json -Depth 3 -Compress'])
    try:
        if result.get('exit_code') != 0:
            raise ValueError('inventory command unavailable')
        adapters = json.loads(result['stdout'] or '[]')
        if adapters is None:
            adapters = []
        elif isinstance(adapters, dict):
            adapters = [adapters]
        if not isinstance(adapters, list) or any(not isinstance(a, dict) for a in adapters):
            raise ValueError('unexpected adapter inventory')
        report['adapters'] = adapters
        wireless = [a for a in adapters if a.get('InterfaceType') == 71
                    or a.get('NdisPhysicalMedium') in (1, 9)]
        report['native_80211_candidates'] = len(wireless)
        report['verdict'] = 'requires_real_pairing_probe' if wireless else 'no_native_80211_adapter'
    except (ValueError, TypeError):
        report['verdict'] = 'unknown'
        report['inventory_error'] = result
    report['wlan_driver_query'] = command(['netsh', 'wlan', 'show', 'drivers'])
else:
    sdk_paths = [os.environ.get('ANDROID_SDK_ROOT'), os.environ.get('ANDROID_HOME')]
    revision = None
    for sdk in sdk_paths:
        if not sdk:
            continue
        props = Path(sdk) / 'emulator/source.properties'
        if props.is_file():
            match = re.search(r'^Pkg.Revision\s*=\s*(.+)$', props.read_text(), re.M)
            if match:
                revision = match.group(1).strip()
                break
    report['emulator_revision'] = revision
    match = re.match(r'^(\d+)\.(\d+)', revision or '')
    report['emulator_meets_documented_36_5_minimum'] = bool(match and tuple(map(int, match.groups())) >= (36, 5))
    report['kvm_read_write'] = os.access('/dev/kvm', os.R_OK | os.W_OK)
    report['verdict'] = 'dual_avd_p2p_not_yet_tested'
print('DIRECT_CAPABILITIES ' + json.dumps(report, ensure_ascii=False))
