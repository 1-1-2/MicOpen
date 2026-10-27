#!/usr/bin/env bash
# 交叉编译 WO Mic Link 客户端（Linux → Windows x64）
#
# 依赖：gcc-mingw-w64-x86-64（apt install gcc-mingw-w64-x86-64）
#       python3（生成驱动包与图标；驱动产物不入库，干净检出时现场生成，见下）
#       deps/opus/（首次跑 tools/build_opus.sh；缺失时自动降级为 opus.dll 动态加载）
#
# 用法：./build.sh [all|app|drvinst|drvinst-dev|test|probe]
#   all          （默认）两个发布产物都编
#   app          只编 build/womic.exe（应用本体：GUI/CLI/网络层，**不含第三方二进制**）
#   drvinst      只编 build/womic-drvinst.exe（驱动安装器：唯一携带驱动包的产物）
#   drvinst-dev  只编 build/womic-drvinst-dev.exe（**未启用的开发分支**：
#                 多带一个「从官方安装包解包驱动」的能力，见下）
#   test         只编 build/test_drvpkg.exe（驱动包 inflate 字节级自检）
#
# 为什么要两个 exe：驱动三件套是第三方已签名二进制（Wolicheng，WHCP 签名）。
# 塞进应用本体 == 每次分发应用都在分发第三方驱动，合规面过大。拆开后：
#   · 已装过驱动的机器只要 womic.exe（体积极小、纯自有代码）；
#   · 没装过的机器才需要多拿一次 womic-drvinst.exe（requireAdministrator 提权）。
# 两者共用同一份安装逻辑（src/drvinst.c），不存在两套实现。
#
# 产物：
#   build/womic.exe          应用。-mwindows（双击不闪黑窗）+ asInvoker
#   build/womic-drvinst.exe  驱动安装器。-mconsole（菜单/脚本都好用）
#                            + requireAdministrator（双击自动弹 UAC）
#   两者都只依赖 Windows 自带 DLL，都内嵌 VS_VERSION_INFO / 图标 / manifest。
#
# 体积预算分开设（安装器不沿用应用那套）：
#   SIZE_BUDGET_APP          应用预算，默认 352 KiB（R-42 撤出 app 侧后回到约 331 KB）
#   SIZE_BUDGET_DRVINST      安装器预算，默认 256 KiB（内嵌三套驱动时实测约 209 KB）
#   SIZE_BUDGET_DRVINST_DEV  开发分支安装器预算，默认 320 KiB（多带解包模块）
#
# 自检：每个产物都要过「体积预算 + 导入表只允许系统 DLL + 资源面断言」。
set -euo pipefail
cd "$(dirname "$0")"

CC=${CC:-x86_64-w64-mingw32-gcc}
WINDRES=${WINDRES:-x86_64-w64-mingw32-windres}
PYTHON=${PYTHON:-python3}
MODE=${1:-all}

APP_OUT=build/womic.exe
DRVINST_OUT=build/womic-drvinst.exe
DRVINST_DEV_OUT=build/womic-drvinst-dev.exe
SIZE_BUDGET_APP=${SIZE_BUDGET_APP:-360448}      # 352 KiB（R-25/R-24：字体字形探测+电平配色+CLIP+卡片裁剪；R-42 撤出 app 侧后体积回落）
SIZE_BUDGET_DRVINST=${SIZE_BUDGET_DRVINST:-262144}  # 256 KiB
SIZE_BUDGET_DRVINST_DEV=${SIZE_BUDGET_DRVINST_DEV:-327680}  # 320 KiB

case "$MODE" in
    all|app|drvinst|drvinst-dev|test|probe) ;;
    *)
        echo "用法: $0 [all|app|drvinst|drvinst-dev|test|probe]" >&2
        exit 1
        ;;
esac

mkdir -p build

# ---------------------------------------------------------------- 资源生成
# 内置驱动包与图标是生成物；源码树上没有时才重新生成（保证离线可构建）。
#
# ⚠ 第三方已签名驱动（womic.sys 等）的二进制**不进入公开源码库**（架构文档 R8），
#   res/drvpkg.bin 与 src/drvpkg_data.h 都在 .gitignore 里。因此：
#   1) 先尝试真实打包（tools/pack_driver.py 会按优先级找官方驱动目录）；
#   2) 找不到驱动时用 --empty 生成合法空包（magic 正确、ver=1、count=0，16 B）
#      兜底，只打 warned 日志、不中断构建 —— 那种安装器需 /driver-pack <dir> 外置驱动；
#   3) app.ico 是自绘图标、与驱动无关，两种模式下都保证能生成。
#   ⚠ drvpkg.bin 只会被 res/drvinst.rc 引用；res/app.rc 里没有它（应用不带驱动）。
if [ ! -f src/drvpkg_data.h ] || [ ! -f res/drvpkg.bin ] || [ ! -f res/app.ico ]; then
    echo "==> 生成内置驱动包与图标"
    if ! "$PYTHON" tools/pack_driver.py; then
        echo "!! warned: 未找到官方驱动目录，改用空包模式（womic-drvinst.exe 不含内置驱动，" \
             "运行时请用 /driver-pack <dir> 指定；womic.exe 本来就不含驱动包）" >&2
        "$PYTHON" tools/pack_driver.py --empty
    fi
fi

# ---------------------------------------------------------------- Opus
# 静态优先：有 deps/opus 就 -DMICLINK_HAVE_OPUS，opusdec.c 直接调用静态符号；
# 没有就编成“运行时 LoadLibrary(opus.dll)”，功能不缺，只是少一层保障。
# 只应用需要：安装器不解码音频。
OPUS_CFLAGS=()
OPUS_LIBS=()
if [ -f deps/opus/lib/libopus.a ]; then
    echo "==> Opus：静态链接（deps/opus）"
    OPUS_CFLAGS=(-Ideps/opus/include -DMICLINK_HAVE_OPUS=1)
    OPUS_LIBS=(-Ldeps/opus/lib -lopus)
else
    echo "==> Opus：未找到 deps/opus，改为运行时加载 opus.dll（tools/build_opus.sh 可补建）"
fi

# ---------------------------------------------------------------- 资源
echo "==> windres"
"$WINDRES" -I res -i res/app.rc     -o build/app_res.o
"$WINDRES" -I res -i res/drvinst.rc -o build/drvinst_res.o

# ---------------------------------------------------------------- 源清单
APP_SRC=(
    src/main.c
    src/engine.c
    src/monitor_ui.c
    src/devio.c
    src/opusdec.c
    src/drvpkg.c
    src/drvinst.c
    src/singleinst.c
    src/cli.c
    src/theme.c
    src/ui_draw.c
    src/ui_switch.c
    src/ui_layout.c
    src/ui_voicemeter.c
    src/ui_help.c
    src/drvbridge.c
)
APP_LIBS=(
    -lgdi32 -luser32 -lws2_32 -luxtheme -ldwmapi -lshell32 -lole32 -lwinmm
    -lsetupapi -lcrypt32 -lnewdev -ladvapi32 -lcomctl32 -luuid
)
# 安装器：菜单/参数解析 + 安装逻辑 + 驱动包解压 + 设备句柄探测（L1 判据）。
# 刻意不编 UI/网络/Opus —— 它只做驱动装/卸/查。
DRVINST_SRC=(
    src/drvinst_main.c
    src/drvinst.c
    src/drvpkg.c
    src/devio.c
)
DRVINST_LIBS=(
    -lsetupapi -lnewdev -lcrypt32 -ladvapi32 -lole32 -lshell32
)
# 开发分支安装器 = 发布版安装器 + 「从官方安装包解包驱动」模块（宏门控）。
# 源清单与发布版完全一致，只多一个 drvimport_dev.c；产物名也独立，
# 绝不覆盖 build/womic-drvinst.exe（发布产物保持干净）。
DRVINST_DEV_SRC=(
    src/drvinst_main.c
    src/drvinst.c
    src/drvpkg.c
    src/devio.c
    src/drvimport_dev.c
)

# ------------------------------------------------------------ 单测模式
# ./build.sh test —— 编一个只含 drvpkg 的小 exe，校验 9 个驱动文件的字节一致性
if [ "$MODE" = "test" ]; then
    echo "==> 编解压自检程序"
    "$CC" -Os -std=c99 -Wall -D_WIN32_WINNT=0x0A00 -DMICLINK_DRVPKG_EMBED_ARRAY=1 \
        -o build/test_drvpkg.exe tools/test_drvpkg.c src/drvpkg.c \
        -lkernel32 -luser32 -lshell32 -lole32
    if command -v wine >/dev/null 2>&1; then
        echo "==> wine 下运行"
        # 空包构建下没有可校验的驱动文件：test_drvpkg.exe 报告 SKIP 并以退出码 2
        # 结束（不假装 PASS）。显式接住退出码，别让 set -e 抢先退出、丢了这行日志。
        rc=0
        WINEDEBUG=-all wine build/test_drvpkg.exe || rc=$?
        echo "==> test_drvpkg 退出码 $rc"
        exit $rc
    fi
    echo "未安装 wine，已产出 build/test_drvpkg.exe，请拷贝到 Windows 上运行"
    exit 0
fi

# ---------------------------------------------------------------- 开关动画探针
# ./build.sh probe —— R-46：证明开关是矢量渲染且真的在插值（不是两态贴图）。
# 无 WM 环境里点不动开关，只能离屏逐帧渲染、比对滑块圆心位置。
if [ "$MODE" = "probe" ]; then
    echo "==> 编开关动画探针"
    # 探针只关心开关，但 ui_draw.c 里有引用 g_lo / ui_title_h 等布局符号
    # （那是"绘制与命中同源"的要求，开关绘制之外的卡片绘制需要）。
    # 这里提供最小 stub —— 它们在探针路径上根本不会被执行到。
"$CC" -Os -std=c99 -Wall -D_WIN32_WINNT=0x0A00 \
        -o build/switch_anim_probe.exe \
        tools/switch_anim_probe.c src/ui_switch.c src/ui_draw.c src/theme.c \
        -lgdi32 -luser32
    if command -v wine >/dev/null 2>&1; then
        echo "==> wine 下运行"
        WINEDEBUG=-all wine build/switch_anim_probe.exe
        exit $?
    fi
    echo "未安装 wine，已产出 build/switch_anim_probe.exe，请拷贝到 Windows 上运行"
    exit 0
fi

# ---------------------------------------------------------------- 自检函数
check_target() {
    local out="$1" budget="$2" kind="$3"
    local size imports bad
    size=$(stat -c %s "$out" 2>/dev/null || stat -f %z "$out")
    echo "built : $out  (${size} bytes / $((size/1024)) KiB)"
    imports=$(x86_64-w64-mingw32-objdump -p "$out" | awk '/DLL Name:/{print $NF}' | sort -u)
    echo "imports: $(echo "$imports" | tr '\n' ' ')"
    # 只允许系统 DLL：libgcc/libwinpthread/vcruntime 出现即为失败
    bad=$(echo "$imports" | grep -iE "libgcc|libwinpthread|libstdc|vcruntime|msvcp|api-ms-win-crt|^libopus|opus" || true)
    if [ -n "$bad" ]; then
        echo "!! 导入表含非系统 DLL: $bad" >&2
        return 1
    fi
    if [ "$size" -gt "$budget" ]; then
        echo "!! 产物 ${size} B 超过预算 ${budget} B" >&2
        return 1
    fi
    # 资源分发面断言（tools/check_pe.py）
    "$PYTHON" tools/check_pe.py "$out" "$kind" || return 1
    return 0
}

# ---------------------------------------------------------------- 编译
# -Os            体积优先
# -s             去掉符号表
# -mwindows      应用用 GUI 子系统（双击不闪黑窗；CLI 输出靠 AttachConsole 兜）
# -mconsole      安装器用控制台子系统（菜单要读 stdin，脚本要拿到退出码）
# --gc-sections  回收未引用段（把 libopus 的 encoder 全部裁掉）
#
# ⚠ 不要加 -ffunction-sections / -fdata-sections：
#   实测加上后产物从 81 KB 涨到 113 KB（+36%）。这两个选项把每个变量拆成独立
#   section，触发 mingw 的对齐填充，导致 .data 段里 99.7% 是零字节。
#   此结论基于实测五种参数组合，勿轻易推翻。
if [ "$MODE" = "all" ] || [ "$MODE" = "app" ]; then
    echo "==> 编译链接 $APP_OUT"
    "${CXX:-x86_64-w64-mingw32-g++}" -std=c++17 -Os -fno-exceptions -fno-rtti -D_WIN32_WINNT=0x0A00 -Wall -c src/monitor.cpp -o build/monitor.o
    "$CC" -Os -s -mwindows \
        -std=c99 -Wall -Wno-unused-function \
        -D_WIN32_WINNT=0x0A00 \
        "${OPUS_CFLAGS[@]}" \
        -Wl,--gc-sections \
        -o "$APP_OUT" \
        "${APP_SRC[@]}" \
        build/app_res.o build/monitor.o \
        "${OPUS_LIBS[@]}" \
        "${APP_LIBS[@]}"
    check_target "$APP_OUT" "$SIZE_BUDGET_APP" app || {
        echo "!! $APP_OUT 自检未通过" >&2; exit 1; }
fi

if [ "$MODE" = "all" ] || [ "$MODE" = "drvinst" ]; then
    echo "==> 编译链接 $DRVINST_OUT"
    "$CC" -Os -s -mconsole \
        -std=c99 -Wall -Wno-unused-function \
        -D_WIN32_WINNT=0x0A00 \
        -Wl,--gc-sections \
        -o "$DRVINST_OUT" \
        "${DRVINST_SRC[@]}" \
        build/drvinst_res.o \
        "${DRVINST_LIBS[@]}"
    check_target "$DRVINST_OUT" "$SIZE_BUDGET_DRVINST" drvinst || {
        echo "!! $DRVINST_OUT 自检未通过" >&2; exit 1; }
fi

# 开发分支安装器：/import-setup 从官方安装包解包驱动（默认不启用、默认构建里不存在）
if [ "$MODE" = "drvinst-dev" ]; then
    echo "==> 编译链接 $DRVINST_DEV_OUT （开发分支，宏门控 MICLINK_DRVIMPORT_DEV）"
    "$CC" -Os -s -mconsole \
        -std=c99 -Wall -Wno-unused-function \
        -D_WIN32_WINNT=0x0A00 \
        -DMICLINK_DRVIMPORT_DEV=1 \
        -Wl,--gc-sections \
        -o "$DRVINST_DEV_OUT" \
        "${DRVINST_DEV_SRC[@]}" \
        build/drvinst_res.o \
        "${DRVINST_LIBS[@]}"
    check_target "$DRVINST_DEV_OUT" "$SIZE_BUDGET_DRVINST_DEV" drvinst-dev || {
        echo "!! $DRVINST_DEV_OUT 自检未通过" >&2; exit 1; }
    echo "提示: 该产物带 /import-setup（从官方安装包解包驱动），仅供开发调试；"
    echo "      正式分发请用 build/womic-drvinst.exe（不含该能力）。"
fi

echo "OK    : 体积、导入表与资源面自检全部通过"
