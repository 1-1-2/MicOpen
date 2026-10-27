#!/usr/bin/env bash
# 历史看门狗按步骤显示名套默认15min预算，会误杀新版组合Action或人工等待。
# 改为只观察精确run id；执行硬超时由被监督进程与job负责，观察超时不取消运行。
# 0=成功，1=运行失败，75=仍运行；支持从任意工作目录调用。
set -euo pipefail
: "${1:?usage: watch-ci-run.sh RUN_ID}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "$SCRIPT_DIR/labctl.py" watch --run "$1" --timeout "${MAX_WAIT:-60}"
