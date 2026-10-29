"""Native Windows control smoke test; no credentials, microphone or captured audio."""
import ctypes as c
from ctypes import wintypes as w
import os
from pathlib import Path
import subprocess
import sys
import time
import struct
import zlib

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

class BitmapInfoHeader(c.Structure):
    _fields_ = [('size', w.DWORD), ('width', w.LONG), ('height', w.LONG),
                ('planes', w.WORD), ('bits', w.WORD), ('compression', w.DWORD),
                ('image_size', w.DWORD), ('xppm', w.LONG), ('yppm', w.LONG),
                ('used', w.DWORD), ('important', w.DWORD)]

class BitmapInfo(c.Structure):
    _fields_ = [('header', BitmapInfoHeader), ('colors', w.DWORD * 3)]

g = c.windll.gdi32
u.GetDC.argtypes = [w.HWND]; u.GetDC.restype = w.HDC
u.ReleaseDC.argtypes = [w.HWND, w.HDC]
u.PrintWindow.argtypes = [w.HWND, w.HDC, w.UINT]; u.PrintWindow.restype = w.BOOL
g.CreateCompatibleDC.argtypes = [w.HDC]; g.CreateCompatibleDC.restype = w.HDC
g.CreateCompatibleBitmap.argtypes = [w.HDC, c.c_int, c.c_int]; g.CreateCompatibleBitmap.restype = w.HBITMAP
g.SelectObject.argtypes = [w.HDC, w.HGDIOBJ]; g.SelectObject.restype = w.HGDIOBJ
g.DeleteObject.argtypes = [w.HGDIOBJ]
g.DeleteDC.argtypes = [w.HDC]
g.GetDIBits.argtypes = [w.HDC, w.HBITMAP, w.UINT, w.UINT, c.c_void_p, c.POINTER(BitmapInfo), w.UINT]
g.GetDIBits.restype = c.c_int

def capture_window(hwnd, name):
    rect = w.RECT()
    if not u.GetWindowRect(hwnd, c.byref(rect)):
        raise RuntimeError('GetWindowRect failed: ' + name)
    width, height = rect.right - rect.left, rect.bottom - rect.top
    if width <= 0 or height <= 0 or width * height > 16000000:
        raise RuntimeError('invalid screenshot dimensions: ' + name)
    screen = u.GetDC(None)
    dc = g.CreateCompatibleDC(screen)
    bmp = g.CreateCompatibleBitmap(screen, width, height)
    if not screen or not dc or not bmp:
        raise RuntimeError('cannot allocate screenshot bitmap: ' + name)
    old = g.SelectObject(dc, bmp)
    try:
        if not u.PrintWindow(hwnd, dc, 2):
            raise RuntimeError('PrintWindow failed: ' + name)
        g.SelectObject(dc, old)
        info = BitmapInfo()
        info.header.size = c.sizeof(BitmapInfoHeader)
        info.header.width, info.header.height = width, -height
        info.header.planes, info.header.bits = 1, 32
        pixels = (c.c_ubyte * (width * height * 4))()
        if g.GetDIBits(dc, bmp, 0, height, pixels, c.byref(info), 0) != height:
            raise RuntimeError('GetDIBits failed: ' + name)
        bgra = bytes(pixels)
        if len(set(bgra[::max(4, len(bgra) // 4096)])) < 2:
            raise RuntimeError('screenshot appears blank: ' + name)
        rgba = bytearray(len(bgra))
        rgba[0::4], rgba[1::4], rgba[2::4] = bgra[2::4], bgra[1::4], bgra[0::4]
        rgba[3::4] = b'\xff' * (width * height)
        stride = width * 4
        scanlines = b''.join(b'\x00' + rgba[y * stride:(y + 1) * stride] for y in range(height))
        def chunk(kind, data):
            return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
        png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
               chunk(b'IDAT', zlib.compress(scanlines, 6)) + chunk(b'IEND', b''))
        out = Path('probe-results/screenshots'); out.mkdir(parents=True, exist_ok=True)
        (out / name).write_bytes(png)
        print(f'PASS screenshot {name}: {width}x{height}')
    finally:
        g.DeleteObject(bmp); g.DeleteDC(dc); u.ReleaseDC(None, screen)

exe = Path(sys.argv[1]).resolve()
# The CLI uses the same gain mapping as the slider and its snap points.
for args, upper in [(['--gain-table'], '6.0'), (['--gain-table', '--gain-ext'], '14.0')]:
    result = subprocess.run([str(exe), *args], cwd=exe.parent, capture_output=True, timeout=15)
    output = result.stdout.decode('utf-8', errors='replace')
    assert result.returncode == 0 and 'monotonic=YES' in output and f'db(pos=1000)={upper}' in output, output
print('PASS normal/extended gain mapping, endpoints and monotonicity')
env = dict(os.environ, MICLINK_SHOW_MONITOR='1')
p = subprocess.Popen([str(exe)], cwd=exe.parent, env=env)
try:
    panel = None
    for _ in range(100):
        panel = u.FindWindowW('MicLinkMonitorTest', None)
        # CreateWindow may expose a handle before ShowWindow; wait for the tested state.
        if panel and u.IsWindowVisible(panel):
            break
        if p.poll() is not None:
            raise RuntimeError('GUI exited before monitor panel appeared')
        time.sleep(.1)
    assert panel and u.IsWindowVisible(panel), 'monitor panel missing'
    time.sleep(.8)
    capture_window(panel, 'gui-monitor.png')
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
    # Regression: making the address field read-only changes its parent color
    # notification to WM_CTLCOLORSTATIC. It must use the same themed surface.
    g.GetBkColor.argtypes = [w.HDC]; g.GetBkColor.restype = w.DWORD
    for label, x in [('light', client.right - 258), ('dark', client.right - 194)]:
        position = (24 << 16) | x
        u.SendMessageW(main, 0x201, 1, position)
        u.SendMessageW(main, 0x202, 0, position)
        dc = u.GetDC(field)
        try:
            u.SendMessageW(field, 0x00CF, 0, 0)  # EM_SETREADONLY(false)
            assert u.SendMessageW(main, 0x0133, dc, field)  # WM_CTLCOLOREDIT
            editable_color = g.GetBkColor(dc)
            u.SendMessageW(field, 0x00CF, 1, 0)
            assert u.SendMessageW(main, 0x0138, dc, field)  # WM_CTLCOLORSTATIC
            assert g.GetBkColor(dc) == editable_color, 'read-only address lost theme'
            if label == 'dark': assert editable_color != 0xffffff, 'dark field became white'
        finally:
            u.ReleaseDC(field, dc)
        time.sleep(.2)
        capture_window(main, 'gui-readonly-' + label + '.png')
        u.SendMessageW(field, 0x00CF, 0, 0)
    print('PASS main address read-only/editable surface matches in both themes')
    u.PostMessageW(main, 0x8003, 0, 0)  # WM_APP + 3
    help_window = None
    for _ in range(50):
        help_window = u.FindWindowW('MicLinkHelpClass', None)
        if help_window:
            break
        time.sleep(.1)
    assert help_window, 'help window missing'
    time.sleep(.5)
    capture_window(help_window, 'gui-help.png')
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
