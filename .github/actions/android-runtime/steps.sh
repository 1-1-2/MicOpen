#!/usr/bin/env bash
set -euo pipefail
stage="${1:?stage required}"
cd "$GITHUB_WORKSPACE/android"
# android.yml / integration-test / Set up Android SDK tools

if [ "$stage" = "s2" ]; then
bash ../.github/scripts/setup-android-sdk.sh
exit 0
fi

# android.yml / integration-test / Install emulator components

if [ "$stage" = "s3" ]; then
sdkmanager --licenses > /dev/null 2>&1 || true
sdkmanager "platform-tools" "emulator" "system-images;android-34;default;x86_64"
echo "--- 已安装组件 ---"
ls "$ANDROID_HOME/system-images/android-34/default/" 2>/dev/null || true

exit 0
fi

# android.yml / integration-test / Install emulator system dependencies

if [ "$stage" = "s5" ]; then
sudo apt-get update -qq
sudo apt-get install -y -qq --no-install-recommends \
  libpulse0 libnss3 libxdamage1 libxcomposite1 libxcursor1 \
  libxi6 libxtst6 libglu1-mesa
echo "--- 系统依赖安装完成（libpulse 等）---"
dpkg -l libpulse0 2>/dev/null | tail -2 || true

exit 0
fi

# android.yml / integration-test / Verify KVM and AVD prerequisites

if [ "$stage" = "s6" ]; then
bash ../.github/scripts/verify-kvm.sh
exit 0
fi

# android.yml / integration-test / Start emulator (with timeout and progress logs)

if [ "$stage" = "s7" ]; then
bash ../.github/scripts/start-emulator.sh womic_interop android-34
exit 0
fi

# android.yml / integration-test / Install and launch app

if [ "$stage" = "s8" ]; then
bash ../.github/scripts/install-app.sh
exit 0
fi

# android.yml / integration-test / UI screenshots (6 screens + control asserts)

if [ "$stage" = "s9" ]; then
bash ../.github/scripts/ui-capture.sh "$GITHUB_WORKSPACE/probe-results/ui-screenshots"
exit 0
fi

# ci.yml / verify-android / 协议闭环
# C05 的 PTY/ONLCR 与读取缓冲根因已修复并有回归测试。保留两次尝试和首次证据，
# 不再把所有失败先验地归因于模拟器抖动；最终仍以真实退出码与断言为准。

if [ "$stage" = "s11" ]; then
set +e
set -o pipefail
INTEROP_DIR="$GITHUB_WORKSPACE/probe-results/interop"
mkdir -p "$INTEROP_DIR"
rc=1
for attempt in 1 2; do
  echo "===== interop attempt $attempt/2 ====="
  timeout 120 python3 verify/integration/pc_client.py --hold-seconds 12 2>&1 | tee "$INTEROP_DIR/attempt-$attempt.out"
  rc=$?
  # 保留一份"最近一次"的完整输出（含 C05 失败现场抓取），
  # collect-diagnostics.sh 会把 attempt-1/2 两份都收进 artifact
  cp "$INTEROP_DIR/attempt-$attempt.out" "$INTEROP_DIR/interop.out"
  echo "attempt $attempt: pc_client exit=$rc（0=全绿，1=有断言失败，2=环境错误，124=超时）"
  [ "$rc" -eq 0 ] && exit 0
  if [ "$attempt" -eq 1 ]; then
    echo "attempt 1 失败，退避 5s 后重试（环境抖动处置，见本步注释）…"
    sleep 5
  fi
done
echo "两次 attempt 均失败（exit=$rc），判定作业失败"
exit $rc

exit 0
fi

# android.yml / integration-test / Collect diagnostics

if [ "$stage" = "s12" ]; then
bash ../.github/scripts/collect-diagnostics.sh
exit 0
fi
echo "unknown stage" >&2; exit 2
