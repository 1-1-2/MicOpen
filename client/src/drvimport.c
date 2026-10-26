/* drvimport.c — 从官方安装包导入驱动（R-42 实现）
 *
 * 流程（全部在后台线程，UI 不阻塞）：
 *   1) 建临时目录 %TEMP%\miclink-drv-<pid>
 *   2) 优先 7z： 7z.exe x -y -o"<tmp>" "<setup>"
 *      没有 7z 就退回官方安装包自己的静默安装：ShellExecuteEx(setup, "/S /D=<tmp>")
 *      —— NSIS 的 /S 仍会写开始菜单与注册表，日志里必须如实提示。
 *   3) 递归找 womic*.inf / womic*.sys / womic*.cat（优先 NTamd64，其次 NTx86）
 *   4) 复制成 <tmp>\drv\womic.inf|sys|cat（我们的安装器按原名找）
 *   5) drvbridge_run(/install /quiet /driver-pack "<tmp>\drv")
 *
 * 失败一律"说清楚 + 给下一步"，绝不静默：事件面板会有 ERROR 与可执行建议。
 * 临时目录在成功后由调用方删除，失败保留现场并在日志里打印路径。
 */
#include "drvimport.h"
#include "drvbridge.h"
#include <shellapi.h>
#include <process.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#define IMPORT_TIMEOUT_MS 180000      /* 解包/静默安装上限 180 s */
#define MAX_SCAN_DEPTH    6

/* --------------------------------------------------------------- 状态 */
static volatile LONG g_busy;          /* 1 = 线程在跑 */
static volatile LONG g_result;        /* 0 未完成 / 1 成功 / 2 失败 */
static volatile LONG g_exit_code = -1;
static wchar_t       g_temp[MAX_PATH];
static HANDLE        g_thread;
static DrvImportLog  g_log;

static void ilog(int kind, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    if (g_log) g_log(buf, kind);
}

/* --------------------------------------------------------------- 小工具 */
static int is_dir_w(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0;
}

static int is_file_w(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0) ? 1 : 0;
}

static void lower_w(wchar_t *s)
{
    for (; *s; s++) *s = (wchar_t)towlower(*s);
}

static int self_dir_w(wchar_t *dir, int cap)
{
    wchar_t self[MAX_PATH];
    wchar_t *cut;
    size_t len;
    if (!GetModuleFileNameW(NULL, self, MAX_PATH)) return 0;
    self[MAX_PATH - 1] = 0;
    cut = wcsrchr(self, L'\\');
    len = cut ? (size_t)(cut - self) : wcslen(self);
    if (len == 0 || len + 1 > (size_t)cap) return 0;
    memcpy(dir, self, len * sizeof(wchar_t));
    dir[len] = 0;
    return 1;
}

static unsigned long long file_size(const wchar_t *p)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(p, GetFileExInfoStandard, &fad)) return 0;
    return ((unsigned long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
}

/* 递归删除（只用于临时目录） */
static void remove_tree(const wchar_t *dir)
{
    wchar_t pat[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    _snwprintf(pat, MAX_PATH * 2, L"%s\\*", dir);
    pat[MAX_PATH * 2 - 1] = 0;
    h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            wchar_t child[MAX_PATH * 2];
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            _snwprintf(child, MAX_PATH * 2, L"%s\\%s", dir, fd.cFileName);
            child[MAX_PATH * 2 - 1] = 0;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree(child);
            else {
                SetFileAttributesW(child, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(child);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}

/* --------------------------------------------------------------- 扫描 */
/* 文件名规则（不区分大小写）：含 womic + 含 setup/client/install 之一 + 以 .exe 结尾，
 * 且不是 womic.exe / womic-drvinst.exe 自身。 */
static int name_matches(const wchar_t *fn)
{
    wchar_t low[MAX_PATH];
    size_t n;
    wcsncpy(low, fn, MAX_PATH - 1);
    low[MAX_PATH - 1] = 0;
    lower_w(low);
    n = wcslen(low);
    if (n < 8) return 0;
    if (wcscmp(low + n - 4, L".exe") != 0) return 0;
    if (!wcsstr(low, L"womic")) return 0;
    if (!wcsstr(low, L"setup") && !wcsstr(low, L"client") && !wcsstr(low, L"install"))
        return 0;
    /* 排除自身与安装器（含带后缀的变体） */
    if (!wcscmp(low, L"womic.exe")) return 0;
    if (wcsstr(low, L"drvinst")) return 0;
    return 1;
}

static void scan_dir(const wchar_t *dir, wchar_t *best, int cap,
                     unsigned long long *best_size)
{
    wchar_t pat[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    _snwprintf(pat, MAX_PATH * 2, L"%s\\*.exe", dir);
    pat[MAX_PATH * 2 - 1] = 0;
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        wchar_t full[MAX_PATH * 2];
        unsigned long long sz;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!name_matches(fd.cFileName)) continue;
        _snwprintf(full, MAX_PATH * 2, L"%s\\%s", dir, fd.cFileName);
        full[MAX_PATH * 2 - 1] = 0;
        sz = file_size(full);
        if (sz > *best_size) {
            *best_size = sz;
            wcsncpy(best, full, (size_t)cap - 1);
            best[cap - 1] = 0;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

int drvimport_scan(wchar_t *path, int pathcap, wchar_t *name, int namecap)
{
    wchar_t best[MAX_PATH * 2], dir[MAX_PATH];
    unsigned long long best_size = 0;
    const wchar_t *cut;
    best[0] = 0;
    if (path && pathcap > 0) path[0] = 0;
    if (name && namecap > 0) name[0] = 0;

    if (self_dir_w(dir, MAX_PATH)) scan_dir(dir, best, MAX_PATH * 2, &best_size);
    if (GetCurrentDirectoryW(MAX_PATH, dir)) scan_dir(dir, best, MAX_PATH * 2, &best_size);
    if (!best[0]) return 0;

    cut = wcsrchr(best, L'\\');
    if (path) {
        wcsncpy(path, best, (size_t)pathcap - 1);
        path[pathcap - 1] = 0;
    }
    if (name) {
        wcsncpy(name, cut ? cut + 1 : best, (size_t)namecap - 1);
        name[namecap - 1] = 0;
    }
    return 1;
}

/* --------------------------------------------------------------- 找 7z */
static int find_7z(wchar_t *out, int cap)
{
    wchar_t buf[MAX_PATH];
    static const wchar_t *const sub[] = {
        L"\\7-Zip\\7z.exe", NULL
    };
    /* 1) PATH */
    if (SearchPathW(NULL, L"7z.exe", NULL, MAX_PATH, buf, NULL) > 0 && is_file_w(buf)) {
        wcsncpy(out, buf, (size_t)cap - 1);
        out[cap - 1] = 0;
        return 1;
    }
    /* 2) %ProgramFiles% 与 %ProgramFiles(x86)% */
    {
        const wchar_t *envs[2] = { L"ProgramFiles", L"ProgramFiles(x86)" };
        int e, s;
        for (e = 0; e < 2; e++) {
            wchar_t root[MAX_PATH];
            if (GetEnvironmentVariableW(envs[e], root, MAX_PATH) <= 0) continue;
            for (s = 0; sub[s]; s++) {
                _snwprintf(buf, MAX_PATH, L"%s%s", root, sub[s]);
                buf[MAX_PATH - 1] = 0;
                if (is_file_w(buf)) {
                    wcsncpy(out, buf, (size_t)cap - 1);
                    out[cap - 1] = 0;
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* --------------------------------------------------------------- 跑进程 */
/* 起一个进程并等它退出；超时强杀。返回 1 = 拿到退出码。 */
static int run_wait(const wchar_t *exe, const wchar_t *params, DWORD timeout_ms,
                    DWORD *exit_code, int hidden)
{
    SHELLEXECUTEINFOW sei;
    DWORD wait;
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpFile = exe;
    sei.lpParameters = params;
    sei.nShow = hidden ? SW_HIDE : SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) return 0;
    wait = WaitForSingleObject(sei.hProcess, timeout_ms);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(sei.hProcess, 1);
        WaitForSingleObject(sei.hProcess, 5000);
    }
    if (exit_code) GetExitCodeProcess(sei.hProcess, exit_code);
    CloseHandle(sei.hProcess);
    return (wait == WAIT_OBJECT_0);
}

/* --------------------------------------------------------------- 找三件套 */
/* 递归找 womic*.inf；命中 NTamd64 的优先，其次 NTx86，再其次其它。 */
static int score_path(const wchar_t *p)
{
    wchar_t low[MAX_PATH * 2];
    wcsncpy(low, p, MAX_PATH * 2 - 1);
    low[MAX_PATH * 2 - 1] = 0;
    lower_w(low);
    if (wcsstr(low, L"ntamd64")) return 3;
    if (wcsstr(low, L"ntx86")) return 2;
    return 1;
}

static int find_inf_rec(const wchar_t *dir, wchar_t *best, int cap,
                        int *best_score, int depth)
{
    wchar_t pat[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    if (depth > MAX_SCAN_DEPTH) return 0;
    _snwprintf(pat, MAX_PATH * 2, L"%s\\*", dir);
    pat[MAX_PATH * 2 - 1] = 0;
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        wchar_t child[MAX_PATH * 2];
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        _snwprintf(child, MAX_PATH * 2, L"%s\\%s", dir, fd.cFileName);
        child[MAX_PATH * 2 - 1] = 0;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            find_inf_rec(child, best, cap, best_score, depth + 1);
        } else {
            wchar_t low[MAX_PATH];
            wcsncpy(low, fd.cFileName, MAX_PATH - 1);
            low[MAX_PATH - 1] = 0;
            lower_w(low);
            if (wcslen(low) > 4 && !wcsncmp(low, L"womic", 5) &&
                !wcscmp(low + wcslen(low) - 4, L".inf")) {
                int sc = score_path(child);
                if (sc > *best_score) {
                    *best_score = sc;
                    wcsncpy(best, child, (size_t)cap - 1);
                    best[cap - 1] = 0;
                }
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return best[0] != 0;
}

/* 在 inf 同目录里找 womic*.sys / *.cat（同名优先，其次任意 womic*） */
static int find_sibling(const wchar_t *dir, const wchar_t *ext,
                        const wchar_t *prefer_stem, wchar_t *out, int cap)
{
    wchar_t pat[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    wchar_t fallback[MAX_PATH * 2];
    fallback[0] = 0;
    _snwprintf(pat, MAX_PATH * 2, L"%s\\*%s", dir, ext);
    pat[MAX_PATH * 2 - 1] = 0;
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        wchar_t full[MAX_PATH * 2], stem[MAX_PATH], lowf[MAX_PATH], lowp[MAX_PATH];
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        _snwprintf(full, MAX_PATH * 2, L"%s\\%s", dir, fd.cFileName);
        full[MAX_PATH * 2 - 1] = 0;
        /* 同 stems 优先（womic.inf ↔ womic.sys） */
        wcsncpy(stem, fd.cFileName, MAX_PATH - 1);
        stem[MAX_PATH - 1] = 0;
        {
            wchar_t *dot = wcsrchr(stem, L'.');
            if (dot) *dot = 0;
        }
        wcsncpy(lowf, stem, MAX_PATH - 1); lowf[MAX_PATH - 1] = 0; lower_w(lowf);
        wcsncpy(lowp, prefer_stem, MAX_PATH - 1); lowp[MAX_PATH - 1] = 0; lower_w(lowp);
        if (!wcscmp(lowf, lowp)) {
            wcsncpy(out, full, (size_t)cap - 1);
            out[cap - 1] = 0;
            FindClose(h);
            return 1;
        }
        if (!fallback[0]) {
            wcsncpy(fallback, full, MAX_PATH * 2 - 1);
            fallback[MAX_PATH * 2 - 1] = 0;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (fallback[0]) {
        wcsncpy(out, fallback, (size_t)cap - 1);
        out[cap - 1] = 0;
        return 1;
    }
    return 0;
}

/* --------------------------------------------------------------- 主流程 */
static void import_fail(const char *why)
{
    ilog(1, "从安装包导入驱动失败：%s", why);
    ilog(1, "建议：手动运行官方安装包安装一次，或安装 7-Zip 后点本按钮重试。"
            "（失败现场已保留：%ls）", g_temp[0] ? g_temp : L"—");
    InterlockedExchange(&g_result, 2);
}

static unsigned __stdcall import_worker(void *arg)
{
    const wchar_t *setup = (const wchar_t *)arg;
    wchar_t seven[MAX_PATH], params[MAX_PATH * 3];
    wchar_t inf[MAX_PATH * 2], sdir[MAX_PATH * 2], src_sys[MAX_PATH * 2],
            src_cat[MAX_PATH * 2], drvdir[MAX_PATH * 2];
    wchar_t dst[MAX_PATH * 2];
    DWORD code = 99;
    int score = 0, extracted = 0;
    size_t cut;
    const wchar_t *stem;

    ilog(0, "正在从官方安装包导入驱动：%ls", setup);

    /* 1) 临时目录 */
    {
        wchar_t tmp[MAX_PATH];
        if (GetTempPathW(MAX_PATH, tmp) == 0) { import_fail("取不到临时目录"); return 0; }
        _snwprintf(g_temp, MAX_PATH, L"%smiclink-drv-%lu", tmp, (unsigned long)GetCurrentProcessId());
        g_temp[MAX_PATH - 1] = 0;
        CreateDirectoryW(g_temp, NULL);
        if (!is_dir_w(g_temp)) { import_fail("无法创建临时目录"); return 0; }
    }

    /* 2) 解包：7z 优先，退路是官方安装包自己的 /S */
    if (find_7z(seven, MAX_PATH)) {
        ilog(0, "使用 7z 解包：%ls x -y -o\"%ls\" \"%ls\"", seven, g_temp, setup);
        _snwprintf(params, MAX_PATH * 3, L"x -y -o\"%ls\" \"%ls\"", g_temp, setup);
        params[MAX_PATH * 3 - 1] = 0;
        if (!run_wait(seven, params, IMPORT_TIMEOUT_MS, &code, 1) || code != 0) {
            ilog(3, "7z 解包失败（退出码 %lu），改用官方安装包静默安装（/S）",
                 (unsigned long)code);
            extracted = 0;
        } else {
            ilog(2, "7z 解包完成");
            extracted = 1;
        }
    } else {
        ilog(3, "未找到 7-Zip（PATH / Program Files 都没有），改用官方安装包静默安装（/S）");
    }

    if (!extracted) {
        _snwprintf(params, MAX_PATH * 3, L"/S /D=%ls", g_temp);
        params[MAX_PATH * 3 - 1] = 0;
        ilog(0, "静默安装：\"%ls\" %ls（上限 %d s）", setup, params, IMPORT_TIMEOUT_MS / 1000);
        if (!run_wait(setup, params, IMPORT_TIMEOUT_MS, &code, 1)) {
            import_fail("官方安装包静默安装超时或无法启动");
            return 0;
        }
        /* 如实说明副作用：NSIS 的 /S 不只解包，还会写开始菜单/注册表 */
        ilog(3, "官方安装包已静默安装到临时目录（该方式会顺带写入开始菜单/注册表）：%ls",
             g_temp);
        ilog(0, "静默安装退出码 = %lu", (unsigned long)code);
        extracted = 1;
    }

    /* 3) 找三件套 */
    if (!find_inf_rec(g_temp, inf, MAX_PATH * 2, &score, 0)) {
        import_fail("安装包里没有找到 womic*.inf");
        return 0;
    }
    wcsncpy(sdir, inf, MAX_PATH * 2 - 1);
    sdir[MAX_PATH * 2 - 1] = 0;
    cut = (size_t)((wcsrchr(sdir, L'\\') ? wcsrchr(sdir, L'\\') - sdir : 0));
    sdir[cut] = 0;
    /* inf 的词干（womic 之类）用来优先配同名的 sys/cat */
    {
        const wchar_t *base = wcsrchr(inf, L'\\');
        wchar_t tmpname[MAX_PATH];
        wchar_t *dot;
        wcsncpy(tmpname, base ? base + 1 : inf, MAX_PATH - 1);
        tmpname[MAX_PATH - 1] = 0;
        dot = wcsrchr(tmpname, L'.');
        if (dot) *dot = 0;
        stem = tmpname;
        if (!find_sibling(sdir, L".sys", stem, src_sys, MAX_PATH * 2)) {
            import_fail("找到 inf 但同目录没有 womic*.sys");
            return 0;
        }
        if (!find_sibling(sdir, L".cat", stem, src_cat, MAX_PATH * 2)) {
            ilog(3, "同目录没有 womic*.cat：仅有 inf+sys 时签名校验可能失败");
            src_cat[0] = 0;
        }
    }
    ilog(0, "找到驱动三件套：%ls（优先级 %d，3=NTamd64）", inf, score);

    /* 4) 复制到 <tmp>\drv 并改成我们的安装器认的原名 */
    _snwprintf(drvdir, MAX_PATH * 2, L"%s\\drv", g_temp);
    drvdir[MAX_PATH * 2 - 1] = 0;
    CreateDirectoryW(drvdir, NULL);
    _snwprintf(dst, MAX_PATH * 2, L"%s\\womic.inf", drvdir);
    dst[MAX_PATH * 2 - 1] = 0;
    if (!CopyFileW(inf, dst, FALSE)) { import_fail("复制 womic.inf 失败"); return 0; }
    _snwprintf(dst, MAX_PATH * 2, L"%s\\womic.sys", drvdir);
    dst[MAX_PATH * 2 - 1] = 0;
    if (!CopyFileW(src_sys, dst, FALSE)) { import_fail("复制 womic.sys 失败"); return 0; }
    if (src_cat[0]) {
        _snwprintf(dst, MAX_PATH * 2, L"%s\\womic.cat", drvdir);
        dst[MAX_PATH * 2 - 1] = 0;
        if (!CopyFileW(src_cat, dst, FALSE)) {
            ilog(3, "复制 womic.cat 失败：继续尝试安装（可能因未签名被拒）");
        }
    }
    ilog(0, "已整理到 %ls（womic.inf/sys/cat）", drvdir);

    /* 5) 交给安装器（唯一那份安装逻辑），带 /driver-pack 指定外置包 */
    ilog(0, "调用安装器：%ls /install /quiet /driver-pack \"%ls\"",
         DRVINST_EXE_A, drvdir);
    if (!drvbridge_run(DRVOP_INSTALL, drvdir, (int *)&code)) {
        ilog(1, "安装器启动失败：%s", drvbridge_last_error());
        InterlockedExchange(&g_exit_code, (LONG)-1);
        import_fail("无法启动 womic-drvinst.exe");
        return 0;
    }
    InterlockedExchange(&g_exit_code, (LONG)code);
    if (code == 0) {
        ilog(2, "安装器返回 0，等待系统确认驱动状态…");
        InterlockedExchange(&g_result, 1);
    } else {
        import_fail("安装器返回非 0");
    }
    return 0;
}

int drvimport_begin(const wchar_t *setup, DrvImportLog log)
{
    static wchar_t setup_copy[MAX_PATH * 2];
    if (!setup || !setup[0]) return 0;
    if (InterlockedCompareExchange(&g_busy, 1, 0) != 0) return 0;   /* 已在跑 */
    g_log = log;
    InterlockedExchange(&g_result, 0);
    InterlockedExchange(&g_exit_code, (LONG)-1);
    g_temp[0] = 0;
    wcsncpy(setup_copy, setup, MAX_PATH * 2 - 1);
    setup_copy[MAX_PATH * 2 - 1] = 0;
    g_thread = (HANDLE)_beginthreadex(NULL, 0, import_worker, setup_copy, 0, NULL);
    if (!g_thread) {
        InterlockedExchange(&g_busy, 0);
        ilog(1, "无法创建导入线程");
        return 0;
    }
    return 1;
}

int drvimport_busy(void)   { return (int)g_busy; }
int drvimport_result(void) { return (int)g_result; }
int drvimport_exit_code(void) { return (int)g_exit_code; }
const wchar_t *drvimport_temp(void) { return g_temp; }

void drvimport_ack(void)
{
    if (g_thread) { CloseHandle(g_thread); g_thread = NULL; }
    InterlockedExchange(&g_busy, 0);
    InterlockedExchange(&g_result, 0);
}

void drvimport_cleanup(void)
{
    if (g_thread) { CloseHandle(g_thread); g_thread = NULL; }
    if (g_temp[0] && is_dir_w(g_temp)) {
        remove_tree(g_temp);
        ilog(0, "临时目录已清理：%ls", g_temp);
    }
    g_temp[0] = 0;
    InterlockedExchange(&g_busy, 0);
    InterlockedExchange(&g_result, 0);
}
