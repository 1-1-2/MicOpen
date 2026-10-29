"""Hardware-independent C ABI boundaries, run on native Windows.

No radio/physical link success is inferred from these checks.
"""
import ctypes
import pathlib
import sys
import threading

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
