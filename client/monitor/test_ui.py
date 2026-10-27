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
u.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
u.GetWindowRect.restype = w.BOOL
u.WindowFromPoint.argtypes = [w.POINT]
u.WindowFromPoint.restype = w.HWND
u.SetForegroundWindow.argtypes = [w.HWND]
u.GetWindowLongW.argtypes = [w.HWND, c.c_int]
u.GetWindowLongW.restype = c.c_long
u.GetClientRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
u.GetClientRect.restype = w.BOOL
u.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
u.GetWindowTextW.restype = c.c_int
u.SetWindowTextW.argtypes = [w.HWND, w.LPCWSTR]
u.SetWindowTextW.restype = w.BOOL

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
    # A reset click must hit its own control and never change the delay.
    u.SetForegroundWindow(panel)
    reset = u.GetDlgItem(panel, 707)
    r = w.RECT()
    assert u.GetWindowRect(reset, c.byref(r))
    point = w.POINT((r.left + r.right) // 2, (r.top + r.bottom) // 2)
    assert u.WindowFromPoint(point) == reset, 'reset button obscured by another control'
    delay = u.GetDlgItem(panel, 706)
    u.SendMessageW(delay, 0x14E, 16, 0)  # CB_SETCURSEL: 170 ms
    u.SendMessageW(panel, 0x111, 706 | (1 << 16), delay)  # CBN_SELCHANGE
    before = u.SendMessageW(delay, 0x147, 0, 0)  # CB_GETCURSEL
    assert before == 16, 'AEC delay selection failed'
    for _ in range(3):
        u.SendMessageW(reset, 0xF5, 0, 0)
        assert u.SendMessageW(delay, 0x147, 0, 0) == before, 'reset changed AEC delay'
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
    main = u.FindWindowW('WomicLinkWnd', None)
    assert main, 'main window missing'
    # Switch theme while an edit has content and a selection. The window geometry
    # below uses the CI runner's normal 96-DPI desktop.
    field = u.GetDlgItem(main, 100)
    assert field and u.SetWindowTextW(field, '192.168.7.42')
    u.SendMessageW(field, 0x00B1, 3, 7)  # EM_SETSEL
    client = w.RECT()
    assert u.GetClientRect(main, c.byref(client))
    for x in (client.right - 258, client.right - 194):
        position = (24 << 16) | x
        u.SendMessageW(main, 0x201, 1, position)  # WM_LBUTTONDOWN
        u.SendMessageW(main, 0x202, 0, position)  # WM_LBUTTONUP
    value = c.create_unicode_buffer(64)
    u.GetWindowTextW(field, value, len(value))
    assert value.value == '192.168.7.42', 'theme switch changed input text'
    assert u.SendMessageW(field, 0x00B0, 0, 0) == (7 << 16 | 3), 'theme switch changed selection'
    u.PostMessageW(main, 0x8003, 0, 0)  # WM_APP + 3
    help_window = None
    for _ in range(50):
        help_window = u.FindWindowW('MicLinkHelpClass', None)
        if help_window:
            break
        time.sleep(.1)
    assert help_window, 'help window missing'
    assert u.GetWindowLongW(help_window, -16) & 0x00C00000 == 0, 'native title bar still enabled'
    u.SendMessageW(help_window, 0x10, 0, 0)
    print('PASS monitor reset hit target, delay stability, choices, controls and frameless help')
finally:
    main = u.FindWindowW('WomicLinkWnd', None)
    if main:
        u.PostMessageW(main, 0x10, 0, 0)
    try:
        p.wait(timeout=5)
    except subprocess.TimeoutExpired:
        p.terminate()
        p.wait(timeout=5)
