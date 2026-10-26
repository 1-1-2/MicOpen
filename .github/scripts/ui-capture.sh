#!/usr/bin/env bash
# ui-capture.sh —— Android 采集端「界面取证」：五个界面各截一张 + uiautomator 控件断言。
#
# 目的：CI 此前只验协议闭环（能收流），从不验界面——三段导航、音源页、高级页、
# 外观页到底长什么样、控件在不在，没有任何证据。本脚本在模拟器上走一遍并留证。
#
# 约定：
#   - 所有 adb 命令都套 timeout（防挂死，这是本仓库 CI 脚本的铁律）；
#   - 导航按 uiautomator dump 里控件的 **text / content-desc** 定位、点其 bounds
#     中心，**不写死坐标**（换分辨率不坏）；同名文本优先取 clickable="true" 的节点；
#   - 断言失败只 WARN 不 exit 1（首轮以取证为主，截图与 dump 全量上传供人工复核；
#     只有「连 app 都拿不到界面」才 exit 1）。
#
# R-12 同步：抽屉导航已移除，导航改为顶栏三段「连接 / 音源 / 高级」+ 右上角
# 外观（👕，content-desc="外观"）。因此本脚本不再有「打开抽屉」这一步；外观 /
# 关于是独立目的地，用返回键回退到三段外壳。
#
# 用法：bash ui-capture.sh [输出目录]   （默认 <local path omitted>）
set -uo pipefail

OUT="${1:-<local path omitted>}"
PKG=com.miclink.open
mkdir -p "$OUT"

adb_t() { timeout 30 adb "$@"; }

# ------------------------------------------------------------------ 基础操作
# 当前界面 dump 到本地：$OUT/<name>.xml
dump_ui() {  # dump_ui <name>
    local name="$1"
    adb_t shell uiautomator dump /sdcard/ui-capture.xml >/dev/null 2>&1
    adb_t exec-out cat /sdcard/ui-capture.xml > "$OUT/$name.xml" 2>/dev/null
    [ -s "$OUT/$name.xml" ]
}

# 截图 + dump：$OUT/<name>.png / .xml
snap() {  # snap <name>
    local name="$1"
    timeout 30 adb exec-out screencap -p > "$OUT/$name.png" 2>/dev/null
    if [ ! -s "$OUT/$name.png" ]; then
        echo "::warning::screencap $name 失败（空文件）"
    fi
    dump_ui "$name" || echo "::warning::uiautomator dump $name 失败"
}

# 从本地 dump 里找 text 或 content-desc 包含 <needle> 的控件，输出 "x y"
# 同名文本可能既出现在顶栏三段（可点）又出现在页内小标题（不可点）——优先取
# clickable="true" 的节点，避免点到标题上白点一次。
find_center() {  # find_center <xml> <needle>
    python3 - "$1" "$2" <<'PY'
import sys
import xml.etree.ElementTree as ET

xml_path, needle = sys.argv[1], sys.argv[2]
try:
    root = ET.parse(xml_path).getroot()
except Exception:
    sys.exit(1)


def center(node):
    b = node.get("bounds") or ""              # [x1,y1][x2,y2]
    left, top = b.split("][")[0].strip("[").split(",")
    right, bottom = b.split("][")[1].strip("]").split(",")
    return (int(left) + int(right)) // 2, (int(top) + int(bottom)) // 2


# 两遍法：① 收集所有 text/desc 匹配节点；② 对每个匹配点，找**包含该点的
# 面积最小可点节点**（Compose 的可点容器常常是空文本的 View，文字只是
# 其不可点子节点——只按文本找 clickable 会一无所获，兜底又会点错别处，
# 真机 02-source 实测踩过）。多候选取面积最小（顶栏分段 < 摘要行 < 卡片）。
clickable = []   # (area, x, y)：最小可点祖先
fallback = None
def area_of(b):
    l, t = b.split("][")[0].strip("[").split(",")
    r, b2 = b.split("][")[1].strip("]").split(",")
    return (int(r) - int(l)) * (int(b2) - int(t))

all_nodes = []
for node in root.iter("node"):
    text = node.get("text") or ""
    desc = node.get("content-desc") or ""
    b = node.get("bounds") or ""
    try:
        x, y = center(node)
    except Exception:
        continue
    all_nodes.append((node, text, desc, x, y, b))
    if needle in text or needle in desc:
        if node.get("clickable") == "true":
            clickable.append((area_of(b), x, y))
        elif fallback is None:
            fallback = (x, y)

if not clickable and fallback:
    fx, fy = fallback
    for node, text, desc, x, y, b in all_nodes:
        if node.get("clickable") == "true":
            lt = b.split("][")[0].strip("[").split(",")
            rb = b.split("][")[1].strip("]").split(",")
            l, t, r, b2 = int(lt[0]), int(lt[1]), int(rb[0]), int(rb[1])
            if l <= fx <= r and t <= fy <= b2:
                clickable.append(((r - l) * (b2 - t), (l + r) // 2, (t + b2) // 2))

if clickable:
    clickable.sort()
    print(clickable[0][1], clickable[0][2])
    sys.exit(0)
if fallback:
    print(*fallback)
    sys.exit(0)
sys.exit(1)
PY
}

# 点按 text/content-desc 包含 <needle> 的控件（先 dump 再找）。
# [expect_after]：点按后目标页应出现的文本；未出现则重试一次（真实设备上
# 曾出现「点了音源还在连接页」：点击落在重组中的节点上被丢弃）。
tap_text() {  # tap_text <needle> [expect_after]
    local needle="$1" expect="${2:-}" pos try
    for try in 1 2 3; do
        dump_ui _tap || { echo "::warning::tap_text($needle)：dump 失败"; return 1; }
        pos=$(find_center "$OUT/_tap.xml" "$needle") || { echo "::warning::找不到控件：$needle"; return 1; }
        # shellcheck disable=SC2086
        adb_t shell input tap $pos
        sleep 1
        [ -z "$expect" ] && return 0
        dump_ui _tap2 || return 0
        if grep -qF "$expect" "$OUT/_tap2.xml" 2>/dev/null; then
            return 0
        fi
        echo "::warning::tap_text($needle)：点按后未见「$expect」，重试（$try/3）"
    done
}

# 返回键（外观 / 关于是独立目的地，靠导航栈回退到三段外壳）
press_back() {
    adb_t shell input keyevent 4 >/dev/null 2>&1 || true
    sleep 1
}

# 断言 dump 里包含某文本
assert_text() {  # assert_text <name> <needle> <label>
    if grep -qF "$2" "$OUT/$1.xml" 2>/dev/null; then
        echo "  [PASS] $3：$2"
    else
        echo "  [WARN] $3 未找到：$2"
    fi
}

# ------------------------------------------------------------------ 主流程
echo "[ui] 前置：确保 MainActivity 在前台"
adb_t shell am start -n "$PKG/.MainActivity" >/dev/null 2>&1 || true
sleep 3

echo "[ui] 1/5 连接页（三段导航 + 状态卡 + 主按钮 + 音量 + 摘要）"
snap 01-connect
# R-12：顶栏三段（放在原来「主页」标题的位置）
assert_text 01-connect "连接" "三段导航-连接"
assert_text 01-connect "音源" "三段导航-音源"
assert_text 01-connect "高级" "三段导航-高级"
# 主操作按钮是二态（D-15：文案「启动/停止」）：服务由 install-app.sh 预启动
# （调试音）→ 显示「停止」；未启动时显示「启动」——两者其一即通过，不能写死
# 单态（首轮曾误报 WARN）。
if grep -qF "停止" "$OUT/01-connect.xml" 2>/dev/null || grep -qF "启动" "$OUT/01-connect.xml" 2>/dev/null; then
    echo "  [PASS] 连接页-主操作按钮：启动/停止（二态任一）"
else
    echo "  [WARN] 连接页-主操作按钮（启动/停止）均未找到"
fi
assert_text 01-connect "推流音量" "连接页-推流音量点轨（D-18）"
# 地址行可见文本只有 IP:端口（无标签），其无障碍 content-desc 带「手机地址」前缀
assert_text 01-connect "手机地址" "连接页-手机地址（D-16）"

echo "[ui] 2/5 音源页（音源 chips + 输入设备 chips）"
tap_text "音源" "跟随系统"
snap 02-source
# D-19/D-20：音源与输入设备已 chips 化
assert_text 02-source "麦克风" "音源页-音源 chips（麦克风档）"
assert_text 02-source "跟随系统" "音源页-输入设备 chips（跟随系统）"

echo "[ui] 3/5 高级页（系统效果 + 控制端口 + 采样率 + 调试音 + 关于）"
tap_text "高级" "关于与帮助"
snap 03-advanced
assert_text 03-advanced "降噪" "高级页-降噪开关"
assert_text 03-advanced "控制端口" "高级页-控制端口"
# R-07：抽屉移除后「关于与帮助」落到高级页底部（文字按钮）
assert_text 03-advanced "关于与帮助" "高级页-关于与帮助（R-07）"

echo "[ui] 4/5 外观页（顶栏右上角 👕 进入）"
tap_text "外观" "主题模式"
sleep 1
snap 04-appearance
assert_text 04-appearance "主题模式" "外观-主题模式"
assert_text 04-appearance "配色" "外观-配色"

echo "[ui] 5/5 复原：回到连接页（不影响后续 interop）"
press_back
tap_text "连接" "推流音量"
snap 05-connect-back

echo "[ui] 完成，产物如下："
ls -la "$OUT"
