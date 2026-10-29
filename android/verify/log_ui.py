"""Exercise the real compact log header; current emulator only, no radio claim."""
from pathlib import Path
import re
import subprocess
import time
import xml.etree.ElementTree as ET

OUT = Path('probe-results/ui-screenshots')
OUT.mkdir(parents=True, exist_ok=True)

def adb(*args):
    return subprocess.check_output(['adb', *args], timeout=30)

def dump(name):
    adb('shell', 'uiautomator', 'dump', '/sdcard/miclink-log-ui.xml')
    raw = adb('exec-out', 'cat', '/sdcard/miclink-log-ui.xml')
    (OUT / (name + '.xml')).write_bytes(raw)
    return ET.fromstring(raw)

def bounds(node):
    return tuple(map(int, re.findall(r'\d+', node.get('bounds', ''))))

def find(root, text):
    for node in root.iter('node'):
        if node.get('text') == text:
            box = bounds(node)
            if len(box) == 4 and box[2] > box[0] and box[3] > box[1]:
                return node
    return None

size = adb('shell', 'wm', 'size').decode()
width, height = map(int, re.findall(r'(\d+)x(\d+)', size)[-1])
old_scale = adb('shell', 'settings', 'get', 'system', 'font_scale').decode().strip()
try:
    for scale in ('1.0', '1.3'):
        adb('shell', 'settings', 'put', 'system', 'font_scale', scale)
        time.sleep(2)
        name = '06-logs-font-' + scale.replace('.', '-')
        for attempt in range(6):
            root = dump(name)
            nodes = [find(root, text) for text in ('运行日志', '清除', '暂停', '复制', '长按可复制')]
            if all(n is not None for n in nodes):
                break
            # Swipe the outer page in its existing left inset, outside nested logs.
            adb('shell', 'input', 'swipe', '6', str(height * 4 // 5), '6', str(height // 4), '250')
        else:
            raise AssertionError('compact log controls missing at font scale ' + scale)
        boxes = [bounds(n) for n in nodes[:4]]
        assert max(b[1] for b in boxes) < min(b[3] for b in boxes), ('log controls wrapped', scale, boxes)
        assert all(boxes[i][2] <= boxes[i + 1][0] for i in range(3)), ('log header overlaps', boxes)
        assert not any('20 秒' in n.get('text', '') or '默认跟随' in n.get('text', '') for n in root.iter('node'))
        (OUT / (name + '.png')).write_bytes(adb('exec-out', 'screencap', '-p'))
        print('PASS compact Android log header, short labels and footer hint; font scale ' + scale)
finally:
    if old_scale == 'null':
        adb('shell', 'settings', 'delete', 'system', 'font_scale')
    else:
        adb('shell', 'settings', 'put', 'system', 'font_scale', old_scale)
    time.sleep(1)
    for _ in range(3):
        adb('shell', 'input', 'swipe', '6', str(height // 4), '6', str(height * 4 // 5), '250')
