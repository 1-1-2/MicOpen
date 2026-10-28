#!/usr/bin/env python3
"""Exercise real Windows process/job cleanup; fixture ADB never touches hardware."""
import argparse
import os
from pathlib import Path
import queue
import shutil
import socket
import subprocess
import tempfile
import threading
import time


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('probe',type=Path)
    ap.add_argument('fake_adb',type=Path)
    ap.add_argument('--wine')
    args=ap.parse_args()
    prefix=[args.wine] if args.wine else []
    def winpath(p): return 'Z:'+str(p.resolve()).replace('/','\\') if args.wine else str(p)
    def listening(port):
        try:
            with socket.create_connection(('127.0.0.1',port),timeout=.2):return True
        except OSError:return False
    def wait_for(check,seconds=6):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            if check():return
            time.sleep(.05)
        raise AssertionError('condition did not become true')
    assert not listening(50370),'isolated fixture port already in use'
    with tempfile.TemporaryDirectory(prefix='miclink-adb-') as td:
        d=Path(td);probe=d/'probe.exe';adb=d/'adb.exe'
        shutil.copy2(args.probe,probe);shutil.copy2(args.fake_adb,adb)
        for case in ['normal','failure','cancel','abrupt','forced','shared']:
            log=d/(case+'.log');env=dict(os.environ,MICLINK_TEST_ADB_LOG=winpath(log),MICLINK_TEST_ADB_CHILD='1')
            env.pop('MICLINK_TEST_ADB_HANG',None)
            if case=='cancel':env['MICLINK_TEST_ADB_HANG']='1'
            existing=None
            with socket.socket() as listener:
                listener.bind(('127.0.0.1',0));listener.listen(4)
                env['MICLINK_TEST_FORWARD_PORT']=str(listener.getsockname()[1]) if case!='failure' else 'invalid'
                try:
                    if case=='shared':
                        shared_env=dict(env);shared_env.pop('MICLINK_TEST_ADB_CHILD')
                        existing=subprocess.Popen(prefix+[str(adb),'-L','tcp:127.0.0.1:50370','server','nodaemon'],env=shared_env)
                        wait_for(lambda:listening(50370))
                    mode='hold' if case=='forced' else case
                    p=subprocess.Popen(prefix+[str(probe),mode],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                    if case=='forced':
                        ready=queue.Queue()
                        threading.Thread(target=lambda:ready.put(p.stdout.readline()),daemon=True).start()
                        assert ready.get(timeout=15).strip()==b'CONNECTED'
                        p.kill()
                    stdout,stderr=p.communicate(timeout=20)
                    if case!='forced':assert p.returncode==0,(case,stdout,stderr)
                    events=[line.split() for line in log.read_text().splitlines()]
                    assert any(e[0]=='CLIENT' for e in events),events
                    own=[int(e[1]) for e in events if not existing or int(e[1])!=existing.pid]
                    # Wine's launcher PID can differ from the Windows PID; identify the
                    # pre-existing server by its known port rather than a host PID.
                    if existing:own=[int(e[1]) for e in events if e[0]!='SERVER']
                    for pid in set(own):
                        wait_for(lambda pid=pid:subprocess.run(prefix+[str(probe),'alive',str(pid)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode!=0)
                    if case in ['normal','abrupt','forced','shared']:
                        assert any(e[0]=='SERVER' for e in events),events
                    if case in ['normal','shared']:assert any(e[0]=='REMOVE' for e in events),events
                    if existing:assert existing.poll() is None and listening(50370),'shared server was killed'
                    print('PASS ADB lifecycle:',case,flush=True)
                finally:
                    if existing:
                        existing.terminate();existing.wait(timeout=5)
                        if args.wine:
                            # Terminating Wine's Unix launcher need not kill its Windows
                            # child; avoid a special-case kill by using fixture logs below.
                            pass
    return 0


if __name__=='__main__':raise SystemExit(main())
