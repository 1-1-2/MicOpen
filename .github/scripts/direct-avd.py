#!/usr/bin/env python3
"""Two AVDs, real WifiP2pManager + bound echo. Never claims Windows WFD coverage."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import time
import xml.etree.ElementTree as ET


class PrerequisiteUnavailable(RuntimeError):
    pass


def image_package(api):
    # Official repository names Android 17's base image android-37.0.
    version = {35: '35', 37: '37.0'}[api]
    return f'system-images;android-{version};google_apis;x86_64'


def validate_avd_metadata(path, api):
    metadata = path.read_text()
    expected = image_package(api).split(';')[1]
    target = next((line.split('=', 1)[1].strip() for line in metadata.splitlines()
                   if line.startswith('target=')), None)
    if target != expected:
        raise RuntimeError(f'Invalid AVD target {target!r}; expected {expected!r}. '
                           'Recreate with Command-line Tools 22.0; old avdmanager can write android-0.')
    return metadata


def run(args, timeout=30, check=True):
    return subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          timeout=timeout, check=check).stdout


def events(log):
    out = []
    for line in log.splitlines():
        start = line.find('{')
        if start >= 0:
            try:
                item = json.loads(line[start:])
                if 'event' in item: out.append(item)
            except ValueError:
                pass
    return out


def p2p_accept_bounds(xml):
    try: root = ET.fromstring(xml[xml.index('<?xml'):])
    except (ValueError, ET.ParseError): return None
    trusted = {'android', 'com.android.settings', 'com.android.wifi.dialog', 'com.google.android.wifi.dialog'}
    nodes = list(root.iter('node'))
    # Restrict to the P2P invitation, not any positive system dialog.
    context = ' '.join(n.get('text', '')+' '+n.get('resource-id', '') for n in nodes if n.get('package') in trusted).lower()
    wifi_dialog_nodes = [n for n in nodes if n.get('package') in
                         {'com.android.wifi.dialog', 'com.google.android.wifi.dialog'}]
    # API 37 renamed this system P2P prompt; require both its title and
    # request text inside the Wi-Fi dialog package, not generic Settings UI.
    device_connection = (any(n.get('text', '').strip().lower() == 'device connection' for n in wifi_dialog_nodes)
                         and any('wants to connect to your device' in n.get('text', '').lower() for n in wifi_dialog_nodes))
    if not device_connection and not any(x in context for x in ['wi-fi direct', 'wifi direct', 'wi‑fi direct', 'p2p', 'invitation to connect']): return None
    for node in nodes:
        if node.get('package') not in trusted: continue
        positive = node.get('resource-id') == 'android:id/button1' or node.get('resource-id', '').endswith('/positive_button')
        if not positive or node.get('enabled') == 'false': continue
        coords = [int(x) for x in re.findall(r'\d+', node.get('bounds', ''))]
        if len(coords) == 4: return ((coords[0]+coords[2])//2, (coords[1]+coords[3])//2)
    return None


def accept_p2p(serial, output):
    run(['adb', '-s', serial, 'shell', 'uiautomator', 'dump', '/sdcard/p2p-ui.xml'], check=False)
    xml = run(['adb', '-s', serial, 'shell', 'cat', '/sdcard/p2p-ui.xml'], check=False)
    (output / (serial+'-latest-ui.xml')).write_text(xml)
    bounds = p2p_accept_bounds(xml)
    if bounds:
        run(['adb', '-s', serial, 'shell', 'input', 'tap', str(bounds[0]), str(bounds[1])])
        print('P2P system invitation accepted on '+serial, flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--apk', type=Path)
    p.add_argument('--print-image-package', type=int, choices=[35, 37])
    p.add_argument('--api', type=int, default=35)
    p.add_argument('--output', type=Path, default=Path('probe-results/direct-avd'))
    a = p.parse_args()
    if a.print_image_package:
        print(image_package(a.print_image_package)); return
    if a.apk is None: p.error('--apk is required for a test')
    a.output.mkdir(parents=True, exist_ok=True)
    report = {'scope': 'Android-emulator-P2P', 'windows_pairing_tested': False,
              'api': a.api, 'status': 'preflight', 'rounds': []}
    manifest = a.apk.parent.parent/'manifest.json'
    if manifest.exists():
        source = json.loads(manifest.read_text())
        report.update(application_source=source['sha'], artifact_run=source['run_id'])
    report['harness_source'] = os.environ.get('GITHUB_SHA', 'local')
    processes = []; streams = []
    sdk = Path(os.environ['ANDROID_HOME']); emulator = sdk/'emulator/emulator'
    serials = ['emulator-5554', 'emulator-5556']
    names = ['miclink_direct_owner', 'miclink_direct_client']
    try:
        version = run([str(emulator), '-version'])
        report['emulator_version'] = version
        match = re.search(r'version\s+(\d+)\.(\d+)', version)
        if not match or tuple(map(int, match.groups())) < (36, 5):
            raise PrerequisiteUnavailable('emulator 36.5+ required')
        if not os.access('/dev/kvm', os.R_OK | os.W_OK): raise PrerequisiteUnavailable('KVM unavailable')
        os.environ['ANDROID_AVD_HOME'] = str(a.output.resolve()/'avds')
        Path(os.environ['ANDROID_AVD_HOME']).mkdir(exist_ok=True)
        image = image_package(a.api)
        report['avd_targets'] = []
        for i, name in enumerate(names):
            subprocess.run(['avdmanager', 'create', 'avd', '-n', name, '-k', image, '--force'],
                           input='no\n', text=True, check=True, timeout=90)
            metadata_path = Path(os.environ['ANDROID_AVD_HOME']) / (name+'.ini')
            metadata = validate_avd_metadata(metadata_path, a.api)
            (a.output / (name+'-metadata.txt')).write_text(metadata)
            report['avd_targets'].append(image.split(';')[1])
            # A fresh Android 17 image can exhaust the default userdata size
            # before PackageInstaller accepts even the first APK.
            config = Path(os.environ['ANDROID_AVD_HOME']) / (name+'.avd') / 'config.ini'
            settings = config.read_text().splitlines()
            settings = [line for line in settings if not line.startswith('disk.dataPartition.size=')]
            config.write_text('\n'.join(settings+['disk.dataPartition.size=4G'])+'\n')
            stream = (a.output/f'emulator-{i}.log').open('w'); streams.append(stream)
            processes.append(subprocess.Popen([str(emulator), '-avd', name, '-port', str(5554+i*2),
                '-no-window', '-no-audio', '-no-snapshot', '-no-boot-anim', '-no-metrics', '-accel', 'on',
                '-gpu', 'swiftshader_indirect', '-memory', '2048', '-cores', '2',
                '-partition-size', '4096'], stdout=stream, stderr=subprocess.STDOUT))
        deadline = time.monotonic()+480
        for serial in serials:
            while time.monotonic() < deadline:
                if any(x.poll() is not None for x in processes): raise RuntimeError('emulator exited during boot')
                if run(['adb', '-s', serial, 'shell', 'getprop', 'sys.boot_completed'], check=False).strip() == '1': break
                time.sleep(2)
            else: raise RuntimeError('prerequisite: emulator boot timeout')
            features = run(['adb', '-s', serial, 'shell', 'pm', 'list', 'features'])
            (a.output/f'{serial}-features.txt').write_text(features)
            if 'android.hardware.wifi.direct' not in features: raise PrerequisiteUnavailable('image lacks wifi.direct feature')
            run(['adb', '-s', serial, 'shell', 'input', 'keyevent', '82'])
            run(['adb', '-s', serial, 'shell', 'wm', 'dismiss-keyguard'], check=False)
            ready_deadline = time.monotonic()+90
            while time.monotonic() < ready_deadline:
                user_state = run(['adb','-s',serial,'shell','am','get-started-user-state','0'], check=False)
                if 'RUNNING_UNLOCKED' in user_state: break
                time.sleep(2)
            else: raise RuntimeError('Android user storage not unlocked: '+user_state.strip())
            print(run(['adb', '-s', serial, 'shell', 'df', '-h', '/data']), flush=True)
            print('Installing debug probe on '+serial, flush=True)
            print(run(['adb', '-s', serial, 'install', '-r', str(a.apk)], timeout=120), flush=True)
            for perm in ['NEARBY_WIFI_DEVICES'] + (['ACCESS_LOCAL_NETWORK'] if a.api >= 37 else []):
                run(['adb', '-s', serial, 'shell', 'pm', 'grant', 'com.miclink.open', 'android.permission.'+perm])
            run(['adb', '-s', serial, 'shell', 'cmd', 'location', 'set-location-enabled', 'true'])
            run(['adb', '-s', serial, 'shell', 'svc', 'wifi', 'enable'])
            run(['adb', '-s', serial, 'shell', 'input', 'keyevent', '82'])
            run(['adb', '-s', serial, 'shell', 'wm', 'dismiss-keyguard'], check=False)
        report['status'] = 'testing'
        for round_index in range(2):
            owner, client = serials
            for serial in serials: run(['adb', '-s', serial, 'logcat', '-c'])  # disposable emulator only
            run(['adb', '-s', owner, 'shell', 'am', 'start', '-n', 'com.miclink.open/.DirectProbeActivity', '--es', 'role', 'owner'])
            deadline = time.monotonic()+45; ready = None
            while time.monotonic() < deadline:
                log = run(['adb', '-s', owner, 'logcat', '-d', '-s', 'DirectProbe:I', '*:S'])
                ready = next((e for e in events(log) if e['event']=='owner_ready'), None)
                if ready: break
                time.sleep(1)
            if not ready: raise RuntimeError('owner did not form a group')
            run(['adb', '-s', client, 'shell', 'am', 'start', '-n', 'com.miclink.open/.DirectProbeActivity', '--es', 'role', 'client'])
            deadline = time.monotonic()+105; client_events = []; owner_events = []
            while time.monotonic() < deadline:
                client_events = events(run(['adb', '-s', client, 'logcat', '-d', '-s', 'DirectProbe:I', '*:S']))
                owner_events = events(run(['adb', '-s', owner, 'logcat', '-d', '-s', 'DirectProbe:I', '*:S']))
                if any(e['event']=='passed' for e in client_events) and any(e['event']=='owner_has_client' for e in owner_events): break
                if any(e['event']=='error' for e in client_events+owner_events): raise RuntimeError('probe reported error; see logs')
                accept_p2p(owner, a.output)
                time.sleep(1)
            else: raise RuntimeError('P2P/echo timeout; shared WLAN echo is not accepted as P2P evidence')
            formed = next(e for e in client_events if e['event']=='client_ready')
            assert formed['formed'] and not formed['isGO'] and formed['interface']
            assert formed['remote']==ready['ip'] and formed['ip']!=ready['ip']
            report['rounds'].append({'round': round_index+1, 'owner': ready, 'client': formed, 'tcp':True, 'udp_packets':10})
            for serial in [client, owner]:
                run(['adb', '-s', serial, 'shell', 'am', 'start', '-n', 'com.miclink.open/.DirectProbeActivity', '--es', 'role', 'stop'])
            for serial in serials:
                (a.output/f'round-{round_index+1}-{serial}.log').write_text(run(['adb', '-s', serial, 'logcat', '-d', '-v', 'threadtime']))
            time.sleep(6)
        report['status']='passed'
    except PrerequisiteUnavailable as e:
        report['status']='unavailable'; report['error']=str(e)
        print('::warning::P2P experiment NOT EXECUTED: '+str(e))
    except Exception as e:
        detail = e.stdout if isinstance(e, subprocess.CalledProcessError) else ''
        report['status']='failed'; report['error']=str(e)+(('\n'+detail) if detail else '')
        if detail: print(detail, flush=True)
        raise
    finally:
        for serial in serials:
            for name, args in [('logcat',['logcat','-d','-v','threadtime']), ('p2p',['shell','dumpsys','wifip2p']), ('addr',['shell','ip','addr']), ('storage',['shell','df','-h','/data'])]:
                try: (a.output/f'{serial}-{name}.txt').write_text(run(['adb','-s',serial]+args, timeout=15, check=False))
                except Exception: pass
        (a.output/'result.json').write_text(json.dumps(report, indent=2)+'\n')
        for process in processes:
            process.terminate()
            try: process.wait(timeout=15)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
        for stream in streams: stream.close()


if __name__ == '__main__': main()
