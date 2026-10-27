"""Native Windows control smoke test; no credentials, microphone or captured audio."""
import ctypes as c
from ctypes import wintypes as w
import os
from pathlib import Path
import subprocess
import sys
import time

u = c.windll.user32
u.FindWindowW.argtypes = [w.LPCWSTR, w.LPCWSTR]
u.FindWindowW.restype = w.HWND
u.GetDlgItem.argtypes = [w.HWND, c.c_int]
u.GetDlgItem.restype = w.HWND
u.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.SendMessageW.restype = c.c_ssize_t
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.PostMessageW.restype = w.BOOL
u.IsWindowVisible.argtypes = [w.HWND]
u.IsWindowVisible.restype = w.BOOL

exe = Path(sys.argv[1]).resolve()
env = dict(os.environ, MICLINK_SHOW_MONITOR='1')
p = subprocess.Popen([str(exe)], cwd=exe.parent, env=env)
try:
    panel = None
    for _ in range(100):
        panel = u.FindWindowW('MicLinkMonitorTest', None)
        if panel:
            break
        if p.poll() is not None:
            raise RuntimeError('GUI exited before monitor panel appeared')
        time.sleep(.1)
    assert panel and u.IsWindowVisible(panel), 'monitor panel missing'
    assert u.SendMessageW(u.GetDlgItem(panel, 702), 0x146, 0, 0) >= 1, 'default device option'
    assert u.SendMessageW(u.GetDlgItem(panel, 703), 0x146, 0, 0) == 2, 'two processing modules'
    assert u.SendMessageW(u.GetDlgItem(panel, 706), 0x146, 0, 0) == 40, 'delay options'
    # Native notification path, same as user selecting a combobox entry.
    for index in [1, 0, 1, 0]:
        combo = u.GetDlgItem(panel, 703)
        u.SendMessageW(combo, 0x14E, index, 0)
        u.SendMessageW(panel, 0x111, 703 | (1 << 16), combo)
    for cid in [701, 704, 704, 701, 707, 708]:
        u.SendMessageW(u.GetDlgItem(panel, cid), 0xF5, 0, 0)  # BM_CLICK
    time.sleep(.5)
    assert p.poll() is None, 'GUI crashed after control changes'
    u.SendMessageW(panel, 0x10, 0, 0)
    assert not u.IsWindowVisible(panel), 'close must hide panel'
    print('PASS monitor panel, choices, native notifications, switches and close-to-hide')
finally:
    main = u.FindWindowW('WomicLinkWnd', None)
    if main:
        u.PostMessageW(main, 0x10, 0, 0)
    try:
        p.wait(timeout=5)
    except subprocess.TimeoutExpired:
        p.terminate()
        p.wait(timeout=5)
