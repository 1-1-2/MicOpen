#!/usr/bin/env python3
"""产物资源自检 —— 各二进制的「分发面」断言。

用法：
  python3 tools/check_pe.py <exe> app
  python3 tools/check_pe.py <exe> drvinst
  python3 tools/check_pe.py <exe> drvinst-dev

判据（读 PE 节表 + 只在 .rsrc 的原始字节范围内找标识，避免把 .rdata 里的
同名字符串常量算进来）：

  app         · .rsrc 非空、VS_VERSION_INFO 已嵌入、产品名存在
              · manifest 必须是 asInvoker（应用本体日常不弹 UAC）
              · **.rsrc 里不许出现 MICLDRV1** —— 应用本体必须一个第三方驱动字节都不带
              · **整份二进制不许出现解包功能的标识串** —— 应用侧不带解包能力（R-42 改道）
  drvinst     · .rsrc 非空、VS_VERSION_INFO 已嵌入、产品名存在
              · manifest 必须是 requireAdministrator（双击自动申请提权）
              · **.rsrc 里必须有 MICLDRV1**，且头合法（ver=1, count<=8）
                count=0 是源码树的空包兜底（合法形态，只提示不判失败）
              · **整份二进制不许出现解包功能的标识串** —— 发布版安装器不带该能力
  drvinst-dev · 与 drvinst 相同的外壳要求
              · **必须出现解包功能的标识串** —— 开发分支确实带上了这个能力
                （否则宏没生效，/import-setup 会变成空壳）

退出码：0 = 全部通过；1 = 有判据不通过。
"""
from __future__ import annotations

import struct
import sys

MAGIC = b"MICLDRV1"

# 解包**实现**的标识串：默认构建里必须**不存在**，开发分支里必须**存在**。
# 注意：命令名 "import-setup" 不算 —— 发布版安装器也要识别这个参数并给出
# 明确提示与下一步（不能静默忽略），所以命令名必然出现在发布版里。
# 这里查的是「解包能力本身」：解包器定位环境变量、7z 的 -aou 参数、
# 以及开发分支独有的帮助条目（发布版 help 里不列这条命令）。
DEV_MARKERS = ["MICLINK_7Z", "x -y -aou", "/import-setup <exe>"]


def fail(msg: str) -> None:
    print("FAIL: " + msg)


def ok(msg: str) -> None:
    print("ok  : " + msg)


def sections(data: bytes) -> list[tuple[str, int, int, int]]:
    """返回 [(name, virtual_size, raw_offset, raw_size)]。"""
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    off = pe + 24 + opt_size
    out = []
    for i in range(nsec):
        base = off + i * 40
        name = data[base:base + 8].rstrip(b"\0").decode("ascii", "replace")
        vsz = struct.unpack_from("<I", data, base + 8)[0]
        raw = struct.unpack_from("<I", data, base + 20)[0]
        size = struct.unpack_from("<I", data, base + 16)[0]
        out.append((name, vsz, raw, size))
    return out


def main(argv: list[str]) -> int:
    if len(argv) != 3 or argv[2] not in ("app", "drvinst", "drvinst-dev"):
        print(__doc__)
        return 1
    path, kind = argv[1], argv[2]
    data = open(path, "rb").read()
    good = True

    def chk(cond: bool, msg: str) -> bool:
        nonlocal good
        (ok if cond else fail)(msg)
        if not cond:
            good = False
        return cond

    secs = sections(data)
    rsrc = [s for s in secs if s[0] == ".rsrc"]
    chk(bool(rsrc) and max(s[1] for s in rsrc) > 8192,
        ".rsrc 段存在且非空 (%d B)" % (max(s[1] for s in rsrc) if rsrc else 0))
    chk("VS_VERSION_INFO".encode("utf-16-le") in data,
        "VS_VERSION_INFO 已嵌入")
    chk("WO Mic Link".encode("utf-16-le") in data, "版本信息里的产品名存在")

    rsrc_blob = b""
    if rsrc:
        # .rsrc 的第一个节：节内数据从 raw 偏移起、最多 size 字节
        off, size = rsrc[0][2], rsrc[0][3]
        rsrc_blob = data[off:off + size]

    if kind == "app":
        chk(b"asInvoker" in data, "manifest 为 asInvoker（日常不弹 UAC）")
        hit = rsrc_blob.find(MAGIC)
        # 应用本体必须零第三方驱动二进制：这是本次拆分的全部意义，
        # 一旦有人把 drvpkg.bin 编回来，这里当场红。
        chk(hit < 0, "应用本体未内置驱动包（.rsrc 内无 MICLDRV1）")
        if hit >= 0:
            print("      ↑ 见 res/app.rc：101 RCDATA drvpkg.bin 不许回去")
        # 应用侧不得带解包能力（R-42 改道：解包整体搬到 drvinst 的开发分支）
        for m in DEV_MARKERS:
            chk(m.encode() not in data and m.encode("utf-16-le") not in data,
                "应用本体不含解包能力标识（%s）" % m)
    else:
        chk(b"requireAdministrator" in data,
            "manifest 为 requireAdministrator（双击自动提权）")
        hit = rsrc_blob.find(MAGIC)
        if not chk(hit >= 0, "内置驱动包 (MICLDRV1) 已嵌入"):
            print("      ↑ 见 res/drvinst.rc：安装器必须携带驱动包")
            return 1
        if hit + 16 <= len(rsrc_blob):
            ver, count = struct.unpack_from("<II", rsrc_blob, hit + 8)
            if ver == 1 and count <= 8:
                if count == 0:
                    print("note: 空包构建（未找到官方驱动目录）—— "
                          "这是干净检出的正常形态，不算失败；"
                          "此安装器需用 /driver-pack <dir> 才有驱动可装")
                ok("内置驱动包头合法 (ver=%d count=%d)" % (ver, count))
            else:
                fail("内置驱动包头不合法 (ver=%d count=%d)" % (ver, count))
                good = False
        else:
            fail("内置驱动包头不完整")
            good = False
        # 解包能力：发布版不得带，开发分支必须带
        if kind == "drvinst-dev":
            for m in DEV_MARKERS:
                chk(m.encode() in data or m.encode("utf-16-le") in data,
                    "开发分支含解包能力标识（%s）" % m)
        else:
            for m in DEV_MARKERS:
                chk(m.encode() not in data and m.encode("utf-16-le") not in data,
                    "发布版安装器不含解包能力标识（%s）" % m)
    return 0 if good else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
