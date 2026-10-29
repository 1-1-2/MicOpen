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
dll.ml_direct_listen.restype = ctypes.c_void_p
dll.ml_direct_listen.argtypes = [ctypes.POINTER(ctypes.c_long), ctypes.c_void_p,
    ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
dll.ml_direct_close.argtypes = [ctypes.c_void_p]
assert dll.ml_direct_abi() == 3
dll.ml_direct_close(None)
assert not dll.ml_direct_listen(None, None, None, None, 0, None, 0, None, 0)

def cancelled_open():
    cancel = ctypes.c_long(1)
    remote, local, error = (ctypes.create_string_buffer(n) for n in [16, 16, 512])
    assert not dll.ml_direct_listen(ctypes.byref(cancel), None, None, remote, 16, local, 16, error, 512)
    assert not remote.value and not local.value and '取消' in error.value.decode('utf-8')
    remote.value = b'unchanged'
    assert not dll.ml_direct_listen(ctypes.byref(cancel), None, None, remote, 1, local, 16, error, 512)
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
    fixture.ml_direct_test_pairing_policy.argtypes = [ctypes.c_int, ctypes.c_int]
    fixture.ml_direct_test_pairing_policy.restype = ctypes.c_int
    # Only successful results or Failed+fresh positive AEP state can advance.
    # In particular, a stale paired flag never overrides refusal/cancellation.
    for state in range(20):
        for fresh in (0, 1):
            expected = state in (0, 3) or (state == 19 and fresh == 1)
            assert bool(fixture.ml_direct_test_pairing_policy(state, fresh)) == expected, (state, fresh)
    for unknown in (-1, 20, 100):
        assert not fixture.ml_direct_test_pairing_policy(unknown, 1)
    print('PASS Direct fresh paired-state recovery preserves all rejection/cancellation boundaries')
    fixture.ml_direct_test_wait.argtypes = [ctypes.POINTER(ctypes.c_long), ctypes.c_uint, ctypes.c_uint]
    fixture.ml_direct_test_wait.restype = ctypes.c_int
    user32 = ctypes.WinDLL('user32', use_last_error=True)
    user32.FindWindowW.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p]
    user32.FindWindowW.restype = ctypes.c_void_p
    user32.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
    for case in ['cancel', 'close', 'timeout']:
        cancel = ctypes.c_long(0)
        results = []
        t = threading.Thread(target=lambda: results.append(fixture.ml_direct_test_wait(ctypes.byref(cancel), 200 if case == 'timeout' else 5000, 0)))
        t.start()
        started = time.monotonic()
        window = None
        while time.monotonic() - started < 2:
            window = user32.FindWindowW('MicLink.WiFiDirect.Listener.v1', None)
            if window: break
            time.sleep(.01)
        assert window, 'listener must open'
        if case == 'cancel': cancel.value = 1
        elif case == 'close': assert user32.PostMessageW(window, 0x0010, 0, 0)
        t.join(timeout=3)
        assert not t.is_alive() and results == [1], (case, results)
        assert not user32.FindWindowW('MicLink.WiFiDirect.Listener.v1', None), 'listener must be destroyed'
        print('PASS Direct native listener ' + case + ' (no radio test)')

    events = []
    LogCallback = ctypes.CFUNCTYPE(None, ctypes.c_char_p, ctypes.c_int)
    logger = LogCallback(lambda line, kind: events.append((line.decode('utf-8'), kind)))
    fixture.ml_direct_set_logger.argtypes = [LogCallback]
    dll.ml_direct_set_logger.argtypes = [LogCallback]
    dll.ml_direct_set_logger(LogCallback())  # Production export also accepts detach.
    for command, mode, expected, logging in [(1, 2, 2, True), (1001, 2, 3, True), (1, 0, 1, True), (1, 2, 2, False)]:
        events.clear()
        fixture.ml_direct_set_logger(logger if logging else LogCallback())
        cancel = ctypes.c_long(0); results = []
        t = threading.Thread(target=lambda: results.append(fixture.ml_direct_test_wait(ctypes.byref(cancel), 5000, mode)))
        t.start(); window = None
        for _ in range(200):
            window = user32.FindWindowW('MicLink.WiFiDirect.Listener.v1', None)
            if window: break
            time.sleep(.01)
        assert window
        time.sleep(.15)
        assert t.is_alive() and not results, 'incoming request must wait for explicit consent'
        user32.PostMessageW(window, 0x0111, command, 0)
        if mode == 0:
            time.sleep(.15)
            assert t.is_alive(), 'Accept without an incoming request must do nothing'
            cancel.value = 1
        t.join(timeout=3)
        assert not t.is_alive() and results == [expected], (command, mode, results)
        assert not user32.FindWindowW('MicLink.WiFiDirect.Listener.v1', None)
        expected_events = []
        if logging and mode == 2:
            expected_events = [('无线直连：用户允许手机连接请求' if command == 1 else '无线直连：用户拒绝手机连接请求', 0)]
        assert events == expected_events, (command, mode, logging, events)
    fixture.ml_direct_set_logger(LogCallback())
    print('PASS Direct stage logger receives actual consent decisions and supports detach')
    print('PASS incoming consent requires a request and explicit Allow; Reject and Stop release the window')

    # Read-only fields send WM_CTLCOLORSTATIC, not WM_CTLCOLOREDIT. Verify both
    # paths retain the same surface color in light/dark native windows.
    user32.GetDlgItem.argtypes = [ctypes.c_void_p, ctypes.c_int]
    user32.GetDlgItem.restype = ctypes.c_void_p
    user32.GetDC.argtypes = [ctypes.c_void_p]; user32.GetDC.restype = ctypes.c_void_p
    user32.ReleaseDC.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    user32.SendMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
    user32.SendMessageW.restype = ctypes.c_ssize_t
    gdi = ctypes.WinDLL('gdi32')
    gdi.GetBkColor.argtypes = [ctypes.c_void_p]; gdi.GetBkColor.restype = ctypes.c_uint
    for dark, expected in [(0, 235 | (237 << 8) | (241 << 16)), (1, 20 | (23 << 8) | (28 << 16))]:
        cancel = ctypes.c_long(0)
        results = []
        t = threading.Thread(target=lambda: results.append(fixture.ml_direct_test_wait(ctypes.byref(cancel), 5000, dark | 2 | 4)))
        t.start()
        started = time.monotonic()
        window = field = None
        while time.monotonic() - started < 2:
            window = user32.FindWindowW('MicLink.WiFiDirect.Listener.v1', None)
            if window:
                field = user32.GetDlgItem(window, 101)
                if field: break
            time.sleep(.01)
        assert window and field
        dc = user32.GetDC(field)
        try:
            for message in [0x0138, 0x0133]:  # WM_CTLCOLORSTATIC / WM_CTLCOLOREDIT
                assert user32.SendMessageW(window, message, dc, field)
                assert gdi.GetBkColor(dc) == expected, 'native field lost theme'
        finally:
            user32.ReleaseDC(field, dc)
            cancel.value = 1
            t.join(timeout=3)
        assert not t.is_alive() and results == [1]
        print('PASS Direct light/dark read-only field surface, compact picker cleanup')
