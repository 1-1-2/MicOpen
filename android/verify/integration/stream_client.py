#!/usr/bin/env python3
"""Exercise the installed Android app through a real ADB forward, no UDP relay.
Run after pc_client.py, which leaves the capture service listening.
"""
import socket
import struct
import subprocess
import time


def adb(*args):
    return subprocess.check_output(['adb', *args], timeout=15, text=True).strip()


def read_frame(sock):
    def exact(n):
        result = b''
        while len(result) < n:
            data = sock.recv(n-len(result))
            if not data:
                raise EOFError('phone disconnected')
            result += data
        return result
    header = exact(5)
    length = int.from_bytes(header[1:], 'big')
    assert length <= 2048, length
    return header[0], exact(length)


def send(sock, kind, data=b''):
    sock.sendall(bytes([kind]) + len(data).to_bytes(4, 'big') + data)


def exercise(serial):
    port = int(adb('-s', serial, 'forward', 'tcp:0', 'tcp:8125'))
    rule = f'{serial} tcp:{port} tcp:8125'
    try:
        assert rule in adb('forward', '--list').splitlines()
        with socket.create_connection(('127.0.0.1', port), timeout=10) as sock:
            send(sock, 101, bytes([4,6,3,0,0,0x53]))
            assert read_frame(sock) == (101, bytes([0,4,2]))
            # CONFIG's media port is legacy metadata, not a UDP destination here.
            send(sock, 102, bytes([2,2]) + struct.pack('>I', 5004))
            assert read_frame(sock) == (102, b'\0')
            send(sock, 103)
            assert read_frame(sock) == (103, b'\0')
            count = 0
            last_sequence = None
            end = time.monotonic() + 7
            heartbeat_at = 0
            while time.monotonic() < end:
                if time.monotonic() >= heartbeat_at:
                    send(sock, 105); heartbeat_at = time.monotonic() + 1
                kind, packet = read_frame(sock)
                assert kind == 200, kind
                assert len(packet) > 11 and packet[:2] == b'\x04\0'
                assert int.from_bytes(packet[2:4], 'big') + 4 == len(packet)
                sequence = int.from_bytes(packet[4:6], 'big')
                if last_sequence is not None:
                    assert (sequence-last_sequence) % 65536 > 0
                last_sequence = sequence
                count += 1
            assert count >= 100, f'only {count} packets'
            send(sock, 104)
            # A few audio packets may already be in flight before STOP is handled.
            while True:
                kind, payload = read_frame(sock)
                if kind != 200:
                    assert (kind, payload) == (102, b'\0')
                    break
            print('PASS Android stream: media, heartbeat, STOP;', count, 'packets', flush=True)
    finally:
        adb('-s', serial, 'forward', '--remove', f'tcp:{port}')
        assert rule not in adb('forward', '--list').splitlines()
    time.sleep(.5)  # Allow phone's connection-owner cleanup to finish before reconnect.


if __name__ == '__main__':
    devices = [line.split()[0] for line in adb('devices').splitlines()[1:] if line.endswith('\tdevice')]
    assert len(devices) == 1, devices
    baseline = set(adb('forward', '--list').splitlines())
    for _ in range(2):
        exercise(devices[0])
    assert set(adb('forward', '--list').splitlines()) == baseline
    print('PASS Android stream: reconnect and forwarding cleanup', flush=True)
