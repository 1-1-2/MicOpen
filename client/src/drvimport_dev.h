/* drvimport_dev.h —— 从官方安装包解包驱动（**未启用的开发分支**）
 *
 * 为什么放在 drvinst 侧而不是应用侧：
 *   · 应用本体（womic.exe）必须零第三方二进制，也不该带解包原厂安装包的能力；
 *   · 解包出来的东西最终要交给驱动安装器 —— 安装逻辑只有一份（drvinst.c），
 *     所以解包能力必须和安装器待在一起，才能走同一条 drv_install() 路径。
 *   · 本模块整体由编译期宏 MICLINK_DRVIMPORT_DEV 门控：默认构建里
 *     **完全不存在**（连字符串都不带），只有 ./build.sh drvinst-dev 才编进去。
 *
 * 同步形态（不是后台线程）：调用方是控制台程序 womic-drvinst.exe，
 *   一次一流程，进度直接 printf，不需要 UI 回调。
 *
 * 依赖 7-Zip：官方 WOMicClientSetup6.3.exe 是 NSIS 包，解包需要外部解包器。
 *   可用环境变量 MICLINK_7Z 指定 7z.exe 路径（测试与特殊环境用）。
 *
 * ⚠ 为什么必须用 `7z x -aou`（auto rename on existing）：
 *   官方安装包 driver/ 目录下有 **5 组同名** womic.inf|womic.sys|womic.cat
 *   （2022 三架构 + 2017 两套）。普通 `7z x` 会让同名文件互相覆盖，
 *   最后写入的 2017 版胜出 —— 而 2017 版的硬件 ID 是 `*WOMic`（通配），
 *   与本项目要求的 `Root\WOMic` 铁律（drvinst.h: WOMIC_HWID）不匹配，
 *   装上也起不来。-aou 会把重名文件改成 womic_1.* / womic_2.* … 全部保留。
 */
#ifndef MICLINK_DRVIMPORT_DEV_H
#define MICLINK_DRVIMPORT_DEV_H

#include <windows.h>

/* 从官方安装包解包驱动三件套，整理到 out_dir（统一改名 womic.inf/womic.sys/womic.cat）。
 *
 * 返回 1 = 成功；返回 0 = 失败，err 里给出「为什么 + 下一步」。
 * tmp_dir（若非空）返回临时工作目录：成功时调用方可直接删掉；
 * 失败时保留现场供人工核查。
 */
int drvimport_dev_extract(const wchar_t *setup,
                           wchar_t *out_dir, int out_cap,
                           wchar_t *err, int err_cap,
                           wchar_t *tmp_dir, int tmp_cap);

/* 删除临时工作目录（递归）。传入空串则什么都不做。 */
void drvimport_dev_cleanup(const wchar_t *tmp_dir);

#endif /* MICLINK_DRVIMPORT_DEV_H */
