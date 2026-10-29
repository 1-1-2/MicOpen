"""Hardware-independent C ABI boundaries, run on native Windows.

No radio/physical link success is inferred from these checks.
"""
import ctypes
import pathlib
import sys
import threading
import time

path = pathlib.Path(sys.argv[1]).resolve()
dll = ctypes.CDLL(str(path))
dll.ml_direct_abi.restype = ctypes.c_uint
dll.ml_direct_open.restype = ctypes.c_void_p
dll.ml_direct_open.argtypes = [ctypes.POINTER(ctypes.c_long), ctypes.c_void_p,
    ctypes.c_int, ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
dll.ml_direct_close.argtypes = [ctypes.c_void_p]
assert dll.ml_direct_abi() == 1
dll.ml_direct_close(None)
assert not dll.ml_direct_open(None, None, 0, None, 0, None, 0)

def cancelled_open():
    cancel = ctypes.c_long(1)
    remote, local, error = (ctypes.create_string_buffer(n) for n in [16, 16, 512])
    assert not dll.ml_direct_open(ctypes.byref(cancel), remote, 16, local, 16, error, 512)
    assert not remote.value and not local.value
    assert '取消' in error.value.decode('utf-8')
    # Too-small endpoint buffers fail before writing or touching the radio.
    remote.value = b'unchanged'
    assert not dll.ml_direct_open(ctypes.byref(cancel), remote, 1, local, 16, error, 512)
    assert remote.value == b'unchanged'

errors = []
def run():
    try:
        for _ in range(32): cancelled_open()
    except BaseException as e: errors.append(e)
t = threading.Thread(target=run)
t.start(); t.join(timeout=10)
assert not t.is_alive(), 'cancel must return immediately'
if errors: raise errors[0]
print('PASS Wi-Fi Direct ABI / invalid arguments / cancellation / repeated calls (no radio test)')

if len(sys.argv) > 2:
    fixture = ctypes.CDLL(str(pathlib.Path(sys.argv[2]).resolve()))
    fixture.ml_direct_test_wait.argtypes = [ctypes.POINTER(ctypes.c_long), ctypes.c_uint]
    fixture.ml_direct_test_wait.restype = ctypes.c_int
    user32 = ctypes.WinDLL('user32', use_last_error=True)
    user32.FindWindowW.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p]
    user32.FindWindowW.restype = ctypes.c_void_p
    user32.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
    for case in ['cancel', 'close', 'timeout']:
        cancel = ctypes.c_long(0)
        results = []
        t = threading.Thread(target=lambda: results.append(fixture.ml_direct_test_wait(ctypes.byref(cancel), 200 if case == 'timeout' else 5000)))
        t.start()
        started = time.monotonic()
        window = None
        while time.monotonic() - started < 2:
            window = user32.FindWindowW('MicLink.WiFiDirect.Selector.v1', None)
            if window: break
            time.sleep(.01)
        assert window, 'selector must open'
        if case == 'cancel': cancel.value = 1
        elif case == 'close': assert user32.PostMessageW(window, 0x0010, 0, 0)
        t.join(timeout=3)
        assert not t.is_alive() and results == [1], (case, results)
        assert not user32.FindWindowW('MicLink.WiFiDirect.Selector.v1', None), 'selector must be destroyed'
        print('PASS Direct native selector ' + case + ' (no radio test)')
