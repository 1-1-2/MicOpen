/* drvimport_dev.c —— 从官方安装包解包驱动三件套（**未启用的开发分支**）
 *
 * 编译期门控：整个文件包在 #ifdef MICLINK_DRVIMPORT_DEV 里。
 *   默认构建（./build.sh all / app / drvinst）里本文件根本不参与编译，
 *   产物里连「官方安装包」这几个字都不会出现。
 *   只有 ./build.sh drvinst-dev 会打开这个宏，产出独立的
 *   build/womic-drvinst-dev.exe（不覆盖发布产物 womic-drvinst.exe）。
 *
 * ── 流程 ────────────────────────────────────────────────────────────────
 *   1) 建临时目录 %TEMP%\miclink-drvimport-<pid>
 *   2) 找 7z.exe（MICLINK_7Z 可覆盖路径），执行
 *        7z x -y -aou -o"<tmp>" "<setup>"
 *      -aou 是**关键**：官方包 driver/ 下有 5 组同名 womic.inf|womic.sys|womic.cat
 *      （2022 三架构 + 2017 两套）。普通 7z x 会让同名文件互相覆盖，
 *      最后写入的 2017 版胜出 —— 而 2017 版硬件 ID 是通配的 *WOMic，
 *      与本项目铁律（drvinst.h: WOMIC_HWID = Root\WOMic）不匹配，装上也起不来。
 *   3) 递归找 womic*.inf，逐个做 **2022 指纹校验**（见下）
 *   4) 选本机架构那一份；用「inf 摘要」在同目录 .cat 里精确定位配套 catalog
 *   5) 三件套复制成 womic.inf|womic.sys|womic.cat 到 <tmp>\drv
 *
 * ── 2022 指纹校验（为什么必须有）────────────────────────────────────────
 *   官方包同时塞了 2017 版和 2022 版，文件名几乎一样（只差 -aou 加的后缀）。
 *   两版关键差别：
 *     2017 版：DriverVer = 05/06/2017, 1.5.0.0，硬件 ID 是通配 *WOMic，
 *              inf 正文里**没有** [MyMfgName.NTxxx] 段
 *     2022 版：DriverVer = 01/13/2022, 2.1.0.0，硬件 ID 是 Root\WOMic，
 *              有 [MyMfgName.NTx86 / NTamd64 / NTARM64] 段
 *   本项目铁律：硬件 ID 必须精确为 Root\WOMic，2017 版**一律不用**。
 *   所以三项必须同时满足才算通过：
 *     a) DriverVer 年份 >= 2022
 *     b) 正文含 Root\WOMic（排除 *WOMic 通配版）
 *     c) 正文含 [MyMfgName.NT<本机架构>]（确认是本机那一套）
 *   任何一项不满足 → 明确报错并列出实际候选，**绝不硬装**。
 *
 * ── cat 与 inf 的配对（为什么用摘要而不是文件名）────────────────────────
 *   实测：官方包里 .cat 内嵌的是 **inf 的 SHA-1/SHA-256 摘要**（不是 sys 的），
 *   而且编号并不与 inf 同 stem 对齐（2017 组的 cat 是 womic_5/womic_6，
 *   对应的 inf 却是 womic_3/womic_4）。所以：
 *     · 主路径：用 inf 的 SHA-256 摘要去 .cat 里搜，命中即配套 catalog（精确）
 *     · 兜底：摘要搜不到时用同 stem 的 .cat，并在输出里标明是兜底
 *   sys ↔ inf 用 PE machine 交叉校验（0x14c / 0x8664 / 0xAA64 必须与
 *   inf 声明的架构段一致），对不上就拒绝安装。
 *
 * 失败一律「说清楚 + 给下一步」，绝不静默。失败时临时目录保留现场。
 */
#ifdef MICLINK_DRVIMPORT_DEV

#define WIN32_LEAN_AND_MEAN
#include "drvimport_dev.h"
#include "drvpkg.h"
#include <shellapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <wincrypt.h>

#define DEV_TIMEOUT_MS   180000     /* 解包上限 180 s */
#define DEV_MAX_DEPTH    6
#define DEV_MAX_INF      64         /* 候选 inf 上限（防病态目录） */

/* PE machine 常量 */
#define PE_MACHINE_I386  0x014c
#define PE_MACHINE_AMD64 0x8664
#define PE_MACHINE_ARM64 0xAA64

/* --------------------------------------------------------------- 输出 */
/* 控制台是 UTF-8（drvinst_main 已 SetConsoleOutputCP(CP_UTF8)），
 * 但 Windows 端 %S/wprintf 行为依实现而异 —— 统一走「宽 → UTF-8 → printf %s」，
 * 与 drvinst_main.c 的 w2u() 同一套路。 */
static void u8(const wchar_t *w, char *buf, size_t cap)
{
    int n;
    if (!w || !w[0]) { if (cap) buf[0] = 0; return; }
    n = WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, (int)cap, NULL, NULL);
    if (n <= 0) buf[0] = 0;
    buf[cap - 1] = 0;
}

static void say(const wchar_t *fmt, ...)
{
    wchar_t wb[1024];
    char mb[3072];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(wb, sizeof(wb) / sizeof(wb[0]) - 1, fmt, ap);
    va_end(ap);
    wb[sizeof(wb) / sizeof(wb[0]) - 1] = 0;
    u8(wb, mb, sizeof(mb));
    printf("%s\n", mb);
}

/* err 缓冲区存宽字符；里面的 %hs / %S 由调用方按窄/宽分别处理 */
static void seterr(wchar_t *err, int cap, const wchar_t *fmt, ...)
{
    va_list ap;
    if (!err || cap <= 0) return;
    va_start(ap, fmt);
    _vsnwprintf(err, (size_t)cap - 1, fmt, ap);
    va_end(ap);
    err[cap - 1] = 0;
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
    return (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) == 0 ? 1 : 0;
}

static void lower_w(wchar_t *s)
{
    for (; *s; s++) *s = (wchar_t)towlower(*s);
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

/* --------------------------------------------------------------- 找 7z */
static int find_7z(wchar_t *out, int cap)
{
    wchar_t buf[MAX_PATH];
    static const wchar_t *const sub[] = { L"\\7-Zip\\7z.exe", NULL };
    int s;
    DWORD n;

    /* 0) 环境变量覆盖（测试与特殊环境） */
    n = GetEnvironmentVariableW(L"MICLINK_7Z", buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH && is_file_w(buf)) {
        wcsncpy(out, buf, (size_t)cap - 1);
        out[cap - 1] = 0;
        return 1;
    }
    /* 1) PATH */
    if (SearchPathW(NULL, L"7z.exe", NULL, MAX_PATH, buf, NULL) > 0 && is_file_w(buf)) {
        wcsncpy(out, buf, (size_t)cap - 1);
        out[cap - 1] = 0;
        return 1;
    }
    /* 2) %ProgramFiles% 与 %ProgramFiles(x86)% */
    {
        const wchar_t *envs[2] = { L"ProgramFiles", L"ProgramFiles(x86)" };
        int e;
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

/* ------------------------------------------------------- inf 文本读取 */
/* inf 可能是 UTF-16LE（2022 版，带 BOM）或 ANSI（2017 版）。
 * 统一转成 ASCII 便于子串匹配：UTF-16LE 取奇数字节（正文关键行全是
 * ASCII 范围字符；非 ASCII 行不影响我们的匹配目标）。 */
static char *read_inf_text(const wchar_t *path, size_t *out_len)
{
    HANDLE h;
    DWORD sz, got = 0;
    char *raw, *txt;
    size_t i, n;

    *out_len = 0;
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    sz = GetFileSize(h, NULL);
    if (sz == 0 || sz > 4 * 1024 * 1024) { CloseHandle(h); return NULL; }
    raw = (char *)malloc(sz + 2);
    if (!raw) { CloseHandle(h); return NULL; }
    if (!ReadFile(h, raw, sz, &got, NULL)) { free(raw); CloseHandle(h); return NULL; }
    CloseHandle(h);

    if (got >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE) {
        n = got / 2;
        txt = (char *)malloc(n + 1);
        if (!txt) { free(raw); return NULL; }
        for (i = 0; i < n; i++) txt[i] = raw[2 * i + 1] == 0 ? raw[2 * i] : '?';
        txt[n] = 0;
        free(raw);
    } else {
        txt = raw;
        txt[got] = 0;
    }
    *out_len = strlen(txt);
    return txt;
}

/* 在 hay 里找 needle（不区分大小写），返回命中位置或 NULL。
 * 必须真的定位到位置 —— 只返回布尔值是不够的：后面要按偏移取字段。 */
static const char *find_ci(const char *hay, const char *needle)
{
    size_t i, nl = strlen(needle);
    if (!nl) return hay;
    for (i = 0; hay[i]; i++) {
        size_t j = 0;
        while (j < nl &&
               tolower((unsigned char)hay[i + j]) == tolower((unsigned char)needle[j]))
            j++;
        if (j == nl) return hay + i;
    }
    return NULL;
}

static int contains_ci(const char *hay, const char *needle)
{
    return find_ci(hay, needle) != NULL;
}

/* DriverVer 的年份：形如 "DriverVer = 01/13/2022, 2.1.0.0"。
 * 返回 0 = 没找到或格式不认识。 */
static int inf_driverver_year(const char *txt)
{
    const char *p = find_ci(txt, "DriverVer");
    const char *q;
    int y = 0, digits = 0;
    if (!p) return 0;
    q = p + 9;
    while (*q == ' ' || *q == '\t' || *q == '=') q++;
    while (*q && *q != '/') q++;      /* 跳过 MM */
    if (*q == '/') q++;
    while (*q && *q != '/') q++;      /* 跳过 DD */
    if (*q != '/') return 0;
    q++;
    while (*q >= '0' && *q <= '9') {
        y = y * 10 + (*q - '0');
        q++;
        digits++;
    }
    return (digits == 4) ? y : 0;
}

/* 架构段名：找 "MyMfgName." 后面到 ']' 的一段（如 NTamd64）。 */
static int inf_arch_tag(const char *txt, char *out, size_t cap)
{
    const char *p = find_ci(txt, "MyMfgName.");
    const char *q;
    size_t i = 0;
    if (!p) { out[0] = 0; return 0; }
    q = p + 10;
    while (*q && *q != ']' && *q != '\r' && *q != '\n' && i + 1 < cap)
        out[i++] = *q++;
    out[i] = 0;
    return i > 0;
}

/* --------------------------------------------------------------- PE 架构 */
static unsigned int sys_machine(const wchar_t *path)
{
    HANDLE h;
    unsigned char head[4096];
    DWORD got = 0;
    unsigned int pe_off, machine = 0;
    if (!is_file_w(path)) return 0;
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    if (!ReadFile(h, head, sizeof(head), &got, NULL) || got < 0x40) {
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    if (head[0] != 'M' || head[1] != 'Z') return 0;
    pe_off = (unsigned int)head[0x3C] | ((unsigned int)head[0x3D] << 8) |
             ((unsigned int)head[0x3E] << 16) | ((unsigned int)head[0x3F] << 24);
    if (pe_off + 6 > got) return 0;
    machine = (unsigned int)head[pe_off + 4] | ((unsigned int)head[pe_off + 5] << 8);
    return machine;
}

/* --------------------------------------------------------------- 摘要 */
/* catalog（.cat）里内嵌的是 **inf** 的摘要，不是 sys 的。
 * 用 SHA-256 搜（实测 inf 的 sha256 与 sha1 都出现在 cat 里，sha256 更长更可靠）。 */
static int file_sha256(const wchar_t *path, unsigned char out[32])
{
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    HANDLE f = INVALID_HANDLE_VALUE;
    unsigned char buf[16384];
    DWORD got = 0;
    int ok = 0;

    if (!CryptAcquireContextW(&prov, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return 0;
    if (!CryptCreateHash(prov, CALG_SHA_256, 0, 0, &hash)) goto done;
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) goto done;
    for (;;) {
        if (!ReadFile(f, buf, sizeof(buf), &got, NULL)) goto done;
        if (got == 0) break;
        if (!CryptHashData(hash, buf, got, 0)) goto done;
    }
    ok = 0;
    {
        DWORD len = 32;
        if (CryptGetHashParam(hash, HP_HASHVAL, out, &len, 0) && len == 32)
            ok = 1;
    }
done:
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    if (hash) CryptDestroyHash(hash);
    if (prov) CryptReleaseContext(prov, 0);
    return ok;
}

/* 在文件字节里搜索一段二进制摘要 */
static int file_contains_blob(const wchar_t *path, const unsigned char *blob, int len)
{
    HANDLE f;
    unsigned char buf[65536];
    DWORD got = 0;
    int hit = 0;
    size_t keep = 0;

    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    for (;;) {
        size_t i, total;
        DWORD n = 0;
        if (!ReadFile(f, buf + keep, (DWORD)(sizeof(buf) - keep), &n, NULL)) break;
        if (n == 0) break;                    /* EOF */
        total = keep + n;
        if (total >= (size_t)len) {
            for (i = 0; i + (size_t)len <= total; i++) {
                if (memcmp(buf + i, blob, (size_t)len) == 0) { hit = 1; break; }
            }
        }
        if (hit) break;
        /* 保留尾部 len-1 字节，跨块匹配 */
        keep = (size_t)len - 1;
        if (keep > total) keep = total;
        if (keep) memmove(buf, buf + total - keep, keep);
    }
    CloseHandle(f);
    return hit;
}

/* --------------------------------------------------------------- 收集 inf */
typedef struct {
    wchar_t path[MAX_PATH * 2];
    wchar_t stem[MAX_PATH];      /* 文件名去扩展名 */
    int      year;
    int      has_root_hwid;      /* 正文含 Root\WOMic */
    char     arch[32];           /* ntx86 / ntamd64 / ntarm64 */
} DevInf;

static int collect_infs(const wchar_t *dir, DevInf *list, int count, int depth)
{
    wchar_t pat[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    if (depth > DEV_MAX_DEPTH || count >= DEV_MAX_INF) return count;
    _snwprintf(pat, MAX_PATH * 2, L"%s\\*", dir);
    pat[MAX_PATH * 2 - 1] = 0;
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return count;
    do {
        wchar_t child[MAX_PATH * 2], low[MAX_PATH];
        wchar_t *dot;
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        _snwprintf(child, MAX_PATH * 2, L"%s\\%s", dir, fd.cFileName);
        child[MAX_PATH * 2 - 1] = 0;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            count = collect_infs(child, list, count, depth + 1);
            if (count >= DEV_MAX_INF) break;
            continue;
        }
        wcsncpy(low, fd.cFileName, MAX_PATH - 1);
        low[MAX_PATH - 1] = 0;
        lower_w(low);
        if (wcslen(low) < 5 || wcscmp(low + wcslen(low) - 4, L".inf") != 0) continue;
        if (wcsncmp(low, L"womic", 5) != 0) continue;
        if (count >= DEV_MAX_INF) break;

        {
            char *txt;
            size_t len, i;
            char arch[32];
            int year;
            txt = read_inf_text(child, &len);
            if (!txt) continue;
            year = inf_driverver_year(txt);
            if (!inf_arch_tag(txt, arch, sizeof(arch))) arch[0] = 0;
            list[count].has_root_hwid = contains_ci(txt, "Root\\WOMic");
            free(txt);
            wcsncpy(list[count].path, child, MAX_PATH * 2 - 1);
            list[count].path[MAX_PATH * 2 - 1] = 0;
            wcsncpy(list[count].stem, fd.cFileName, MAX_PATH - 1);
            list[count].stem[MAX_PATH - 1] = 0;
            dot = wcsrchr(list[count].stem, L'.');
            if (dot) *dot = 0;
            list[count].year = year;
            for (i = 0; i + 1 < sizeof(arch) && arch[i]; i++)
                list[count].arch[i] = (char)towlower((unsigned char)arch[i]);
            list[count].arch[i] = 0;
            count++;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return count;
}

/* 同目录里找 womic*.cat / womic*.sys */
static int find_sibling(const wchar_t *dir, const wchar_t *ext, const wchar_t *stem,
                        wchar_t *out, int cap)
{
    wchar_t pat[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    wchar_t fallback[MAX_PATH * 2];
    fallback[0] = 0;

    _snwprintf(pat, MAX_PATH * 2, L"%s\\womic*.%s", dir, ext);
    pat[MAX_PATH * 2 - 1] = 0;
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        wchar_t full[MAX_PATH * 2];
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        _snwprintf(full, MAX_PATH * 2, L"%s\\%s", dir, fd.cFileName);
        full[MAX_PATH * 2 - 1] = 0;
        if (!_wcsicmp(fd.cFileName, stem)) {
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

/* 在目录里逐个 cat 找「含有该 inf 摘要」的那个 */
static int find_cat_by_digest(const wchar_t *dir, const unsigned char digest[32],
                              wchar_t *out, int cap)
{
    wchar_t pat[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int found = 0;

    _snwprintf(pat, MAX_PATH * 2, L"%s\\womic*.cat", dir);
    pat[MAX_PATH * 2 - 1] = 0;
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        wchar_t full[MAX_PATH * 2];
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        _snwprintf(full, MAX_PATH * 2, L"%s\\%s", dir, fd.cFileName);
        full[MAX_PATH * 2 - 1] = 0;
        if (file_contains_blob(full, digest, 32)) {
            wcsncpy(out, full, (size_t)cap - 1);
            out[cap - 1] = 0;
            found = 1;
            break;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

/* --------------------------------------------------------------- 主流程 */
int drvimport_dev_extract(const wchar_t *setup,
                           wchar_t *out_dir, int out_cap,
                           wchar_t *err, int err_cap,
                           wchar_t *tmp_dir, int tmp_cap)
{
    wchar_t seven[MAX_PATH], params[MAX_PATH * 3];
    wchar_t tmp[MAX_PATH], drvdir[MAX_PATH * 2], sdir[MAX_PATH * 2];
    wchar_t src_inf[MAX_PATH * 2], src_sys[MAX_PATH * 2], src_cat[MAX_PATH * 2];
    wchar_t dst[MAX_PATH * 2];
    DevInf *list = NULL;
    int count = 0, i, pick = -1;
    DWORD code = 99;
    unsigned int host_machine = 0;
    const char *host_tag;
    unsigned char digest[32];

    if (tmp_dir && tmp_cap > 0) tmp_dir[0] = 0;
    if (err && err_cap > 0) err[0] = 0;
    if (out_dir && out_cap > 0) out_dir[0] = 0;
    if (!setup || !setup[0]) {
        seterr(err, err_cap, L"没有指定官方安装包路径");
        return 0;
    }
    if (!is_file_w(setup)) {
        seterr(err, err_cap, L"官方安装包不存在或不是文件：%ls", setup);
        return 0;
    }

    switch (drvpkg_detect_arch()) {
    case DRVARCH_AMD64: host_tag = "ntamd64"; host_machine = PE_MACHINE_AMD64; break;
    case DRVARCH_ARM64: host_tag = "ntarm64"; host_machine = PE_MACHINE_ARM64; break;
    case DRVARCH_INTEL: host_tag = "ntx86";   host_machine = PE_MACHINE_I386;  break;
    default:
        seterr(err, err_cap, L"无法识别本机处理器架构");
        return 0;
    }

    /* 1) 临时目录 */
    {
        wchar_t base[MAX_PATH];
        if (GetTempPathW(MAX_PATH, base) == 0) {
            seterr(err, err_cap, L"取不到临时目录");
            return 0;
        }
        _snwprintf(tmp, MAX_PATH, L"%smiclink-drvimport-%lu", base,
                   (unsigned long)GetCurrentProcessId());
        tmp[MAX_PATH - 1] = 0;
    }
    CreateDirectoryW(tmp, NULL);
    if (!is_dir_w(tmp)) {
        seterr(err, err_cap, L"无法创建临时目录：%ls", tmp);
        return 0;
    }
    if (tmp_dir && tmp_cap > 0) {
        wcsncpy(tmp_dir, tmp, (size_t)tmp_cap - 1);
        tmp_dir[tmp_cap - 1] = 0;
    }
    say(L"临时目录：%ls", tmp);

    /* 2) 解包 */
    if (!find_7z(seven, MAX_PATH)) {
        seterr(err, err_cap,
               L"找不到 7z.exe，无法解包 NSIS 格式的官方安装包。\n"
               L"      请安装 7-Zip 后重试，或用环境变量 MICLINK_7Z 指定 7z.exe 路径。");
        return 0;
    }
    say(L"使用 7z：%ls", seven);
    /* -aou：同名文件自动改名（womic_1.* / womic_2.* …），保留全部版本。
     * 少了这个参数，官方包里的 5 组同名三件套会互相覆盖、只剩 2017 版。 */
    _snwprintf(params, MAX_PATH * 3, L"x -y -aou -o\"%ls\" \"%ls\"", tmp, setup);
    params[MAX_PATH * 3 - 1] = 0;
    say(L"解包：%ls x -y -aou -o\"%ls\" \"%ls\"", seven, tmp, setup);
    if (!run_wait(seven, params, DEV_TIMEOUT_MS, &code, 1) || code != 0) {
        seterr(err, err_cap,
               L"7z 解包失败（退出码 %lu）。\n"
               L"      请确认这个文件确实是官方 NSIS 安装包（可用 7z l 手工核对条目）。",
               (unsigned long)code);
        return 0;
    }
    say(L"解包完成（7z 退出码 0）");

    /* 3) 收集并校验 inf */
    list = (DevInf *)calloc(DEV_MAX_INF, sizeof(DevInf));
    if (!list) {
        seterr(err, err_cap, L"内存不足");
        return 0;
    }
    count = collect_infs(tmp, list, 0, 0);
    if (count == 0) {
        seterr(err, err_cap,
               L"解包结果里没有找到 womic*.inf。\n"
               L"      这个安装包可能不是 WO Mic 官方包；现场保留在：%ls", tmp);
        free(list);
        return 0;
    }

    say(L"找到 %d 个 womic*.inf，逐个做 2022 指纹校验：", count);
    for (i = 0; i < count; i++) {
        char u8stem[256], u8arch[64];
        int pass;
        u8(list[i].stem, u8stem, sizeof(u8stem));
        strncpy(u8arch, list[i].arch, sizeof(u8arch) - 1);
        u8arch[sizeof(u8arch) - 1] = 0;
        pass = (list[i].year >= 2022) && list[i].has_root_hwid &&
               (strcmp(list[i].arch, host_tag) == 0);
        say(L"  [%d] %hs  年份=%d  Root\\WOMic=%s  架构=%hs  -> %s",
            i, u8stem, list[i].year,
            list[i].has_root_hwid ? "有" : "无", u8arch,
            pass ? "通过" : "不通过");
        if (pass && pick < 0) pick = i;
    }

    if (pick < 0) {
        wchar_t cand[2048];
        size_t used = 0;
        cand[0] = 0;
        for (i = 0; i < count; i++) {
            char u8stem[256], u8arch[64];
            u8(list[i].stem, u8stem, sizeof(u8stem));
            strncpy(u8arch, list[i].arch, sizeof(u8arch) - 1);
            u8arch[sizeof(u8arch) - 1] = 0;
            used += (size_t)_snwprintf(cand + used, sizeof(cand) / sizeof(cand[0]) - used,
                                       L"      %hs（年份 %d，Root\\WOMic %s，架构 %hs）\n",
                                       u8stem, list[i].year,
                                       list[i].has_root_hwid ? "有" : "无", u8arch);
            if (used >= sizeof(cand) / sizeof(cand[0])) break;
        }
        seterr(err, err_cap,
               L"安装包里没有通过 2022 指纹校验、且属于本机架构（%hs）的驱动。\n"
               L"      候选：\n%s"
               L"      本项目铁律要求硬件 ID 精确为 Root\\WOMic（2017 版的 *WOMic 通配版一律不用）。\n"
               L"      现场保留在：%ls",
               host_tag, cand, tmp);
        free(list);
        return 0;
    }
    {
        char u8stem[256];
        u8(list[pick].stem, u8stem, sizeof(u8stem));
        say(L"选中：%hs（架构 %hs）", u8stem, host_tag);
    }

    /* 4) 定位 sys / cat */
    wcsncpy(sdir, list[pick].path, MAX_PATH * 2 - 1);
    sdir[MAX_PATH * 2 - 1] = 0;
    {
        wchar_t *cut = wcsrchr(sdir, L'\\');
        if (cut) *cut = 0;
    }
    wcsncpy(src_inf, list[pick].path, MAX_PATH * 2 - 1);
    src_inf[MAX_PATH * 2 - 1] = 0;

    /* sys：同 stem 优先，找不到就按目录里唯一一个 PE machine 匹配的 */
    {
        wchar_t want[MAX_PATH];
        _snwprintf(want, MAX_PATH, L"%s.sys", list[pick].stem);
        want[MAX_PATH - 1] = 0;
        if (!find_sibling(sdir, L"sys", want, src_sys, MAX_PATH * 2)) {
            seterr(err, err_cap, L"同目录没有 womic*.sys：%ls", sdir);
            free(list);
            return 0;
        }
    }
    if (sys_machine(src_sys) != host_machine) {
        seterr(err, err_cap,
               L"驱动 sys 的 PE 架构与 inf 声明的不一致（%ls 声明 %hs，sys 是 0x%04X，本机是 0x%04X）。\n"
               L"      三件套配对有误，拒绝安装。",
               list[pick].stem, host_tag, sys_machine(src_sys), host_machine);
        free(list);
        return 0;
    }

    /* cat：主路径用 inf 的 SHA-256 摘要精确配对；兜底用同 stem */
    {
        wchar_t want[MAX_PATH];
        int by_digest = 0;
        if (file_sha256(src_inf, digest)) {
            by_digest = find_cat_by_digest(sdir, digest, src_cat, MAX_PATH * 2);
        }
        if (by_digest) {
            say(L"catalog 由 inf 摘要精确配对：%ls", src_cat);
        } else {
            _snwprintf(want, MAX_PATH, L"%s.cat", list[pick].stem);
            want[MAX_PATH - 1] = 0;
            if (!find_sibling(sdir, L"cat", want, src_cat, MAX_PATH * 2)) {
                seterr(err, err_cap, L"同目录没有 womic*.cat（catalog）：%ls", sdir);
                free(list);
                return 0;
            }
            say(L"注意：未能在 catalog 里匹配到 inf 摘要，改用同 stem 兜底：%ls", src_cat);
        }
    }
    free(list);

    /* 5) 复制成安装器认的原名 */
    _snwprintf(drvdir, MAX_PATH * 2, L"%s\\drv", tmp);
    drvdir[MAX_PATH * 2 - 1] = 0;
    CreateDirectoryW(drvdir, NULL);
    if (!is_dir_w(drvdir)) {
        seterr(err, err_cap, L"无法创建目录：%ls", drvdir);
        return 0;
    }
    _snwprintf(dst, MAX_PATH * 2, L"%s\\womic.inf", drvdir);
    dst[MAX_PATH * 2 - 1] = 0;
    if (!CopyFileW(src_inf, dst, FALSE)) {
        seterr(err, err_cap, L"复制 womic.inf 失败 (gle=%lu)", (unsigned long)GetLastError());
        return 0;
    }
    _snwprintf(dst, MAX_PATH * 2, L"%s\\womic.sys", drvdir);
    dst[MAX_PATH * 2 - 1] = 0;
    if (!CopyFileW(src_sys, dst, FALSE)) {
        seterr(err, err_cap, L"复制 womic.sys 失败 (gle=%lu)", (unsigned long)GetLastError());
        return 0;
    }
    _snwprintf(dst, MAX_PATH * 2, L"%s\\womic.cat", drvdir);
    dst[MAX_PATH * 2 - 1] = 0;
    if (!CopyFileW(src_cat, dst, FALSE)) {
        seterr(err, err_cap, L"复制 womic.cat 失败 (gle=%lu)", (unsigned long)GetLastError());
        return 0;
    }

    if (out_dir && out_cap > 0) {
        wcsncpy(out_dir, drvdir, (size_t)out_cap - 1);
        out_dir[out_cap - 1] = 0;
    }
    say(L"三件套已整理到：%ls", drvdir);
    say(L"  womic.inf %llu B / womic.sys %llu B / womic.cat %llu B",
        file_size(src_inf), file_size(src_sys), file_size(src_cat));
    return 1;
}

void drvimport_dev_cleanup(const wchar_t *tmp_dir)
{
    if (!tmp_dir || !tmp_dir[0]) return;
    if (is_dir_w(tmp_dir)) {
        remove_tree(tmp_dir);
        say(L"临时目录已清理：%ls", tmp_dir);
    }
}

#endif /* MICLINK_DRVIMPORT_DEV */
