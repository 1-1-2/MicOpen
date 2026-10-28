#!/usr/bin/env python3
"""Real Windows executable vs fragmented stream fixture; no radio/ADB/driver required."""
import socket
import struct
import subprocess
import sys
import threading
import time


def frame(kind, payload=b''):
    return bytes([kind]) + struct.pack('>I', len(payload)) + payload


def exact(sock, n):
    data = b''
    while len(data) < n:
        part = sock.recv(n-len(data))
        if not part:
            raise EOFError()
        data += part
    return data


def exercise(exe, scenario):
    errors = []
    ready = threading.Event()
    port = []
    def server():
        try:
            with socket.socket() as listener:
                listener.bind(('127.0.0.1', 0)); listener.listen(1)
                port.append(listener.getsockname()[1]); ready.set()
                listener.settimeout(20)
                with listener.accept()[0] as peer:
                    peer.settimeout(15)
                    for expected in (101,102,103):
                        head = exact(peer,5)
                        payload = exact(peer,struct.unpack('>I',head[1:])[0])
                        assert head[0] == expected
                        if expected == 101:
                            assert payload[-1] == 0x53
                            if scenario == 'closed': return
                            peer.sendall(frame(101,bytes([0,4,0 if scenario=='legacy' else 2])))
                            if scenario == 'legacy': return
                        else: peer.sendall(frame(expected,b'\0'))
                    if scenario == 'oversize':
                        peer.sendall(bytes([200])+struct.pack('>I',2049)); time.sleep(1); return
                    seq = 0
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline:
                        # WO Mic format C, Opus silence, original datagram preserved.
                        packet = struct.pack('>BBHHIB',4,0,10,seq,seq*960,5)+b'\xf8\xff\xfe'
                        wire = frame(200,packet)
                        peer.sendall(wire[:3])
                        if scenario == 'slowheader' and seq == 0: time.sleep(.25)
                        peer.sendall(wire[3:])
                        seq = (seq + 1) & 0xffff
                        time.sleep(.02)
        except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):
            pass  # Client intentionally closes at end of no-GUI smoke window.
        except Exception as e: errors.append(e)
    worker=threading.Thread(target=server,daemon=True); worker.start()
    assert ready.wait(5), 'fixture did not bind'
    p=subprocess.run([exe,'--transport','tcp','--host','127.0.0.1','--port',str(port[0]),'--connect','--no-gui'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=25)
    worker.join(16)
    assert not worker.is_alive(), 'fixture did not stop'
    assert not errors, errors
    if scenario in ('media', 'slowheader'):
        assert p.returncode==0, p.stdout
        # CLI prints ASCII "OK" only after ST_STREAMING; require received media too.
        text=p.stdout.decode('utf-8',errors='replace')
        assert 'OK' in text, text
        import re
        assert re.search(r'帧=[1-9]\d*',text), text
    else:
        assert p.returncode!=0, (scenario,p.stdout)
        if scenario == 'closed': assert '对端已关闭连接' in p.stdout.decode('utf-8', errors='replace'), p.stdout
    print('PASS stream transport:',scenario)


if __name__=='__main__':
    for case in ('media','slowheader','legacy','oversize','closed'): exercise(sys.argv[1],case)
