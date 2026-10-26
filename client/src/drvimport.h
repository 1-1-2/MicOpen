/* drvimport.h — 从官方安装包导入驱动（R-42）
 *
 * 背景：源码库不内置第三方已签名驱动，womic-drvinst.exe 是"空包构建"。
 * 用户机器上如果放着官方安装包（WOMicClientSetup6.3.exe，NSIS 封装，内含
 * womic.inf/sys/cat 三件套），我们可以就地取材：解包 → 找出三件套 →
 * 复制成我们的安装器认的原名 → 走 /driver-pack 装（复用唯一那份安装逻辑）。
 *
 * 不动的东西：不改安装器、不改打包脚本、不内置任何第三方二进制。
 * 提取优先 7z（若装了 7-Zip），没有 7z 就退回官方安装包自己的 /S 静默安装。
 */
#ifndef MICLINK_DRVIMPORT_H
#define MICLINK_DRVIMPORT_H

#include "app.h"

/* 日志回调：与引擎日志同形（0 INFO / 1 ERROR / 2 OK / 3 WARN） */
typedef void (*DrvImportLog)(const char *line, int kind);

/* 扫描官方安装包：exe 同目录 + 当前工作目录。
 * 规则（不区分大小写）：文件名含 "womic"，且含 setup/client/install 之一，
 * 且以 ".exe" 结尾；排除 womic.exe / womic-drvinst.exe 自身。
 * 命中多个取**体积最大**的一个（真安装包 ~1.7 MB，避免误选小工具）。
 * 返回 1 = 找到（path/name 已填），0 = 没找到（path/name 置空）。 */
int drvimport_scan(wchar_t *path, int pathcap, wchar_t *name, int namecap);

/* 后台导入：立即返回；进度/结果全部通过 log 上报。重复调用（正在跑）返回 0。 */
int  drvimport_begin(const wchar_t *setup, DrvImportLog log);

int  drvimport_busy(void);          /* 1 = 还在跑 */
int  drvimport_result(void);        /* 0 未完成 / 1 安装器退出码 0 / 2 失败 */
int  drvimport_exit_code(void);     /* 安装器退出码（失败为 -1） */
const wchar_t *drvimport_temp(void);/* 临时目录（失败时保留供排查） */
void drvimport_cleanup(void);       /* 递归删除临时目录（成功后由调用方触发） */
/* 收尾但不删现场：失败分支用（保留目录供排查，日志已打印路径）。 */
void drvimport_ack(void);

#endif /* MICLINK_DRVIMPORT_H */
