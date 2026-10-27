#!/usr/bin/env bash
set -euo pipefail
stage="${1:?stage required}"
cd "$GITHUB_WORKSPACE/android"
# tailscale-dual.yml / linux-app / Ensure wine (diagnostic only) + record version

if [ "$stage" = "s7" ]; then
mkdir -p "$GITHUB_WORKSPACE/probe-results/logs"
if wine --version > "$GITHUB_WORKSPACE/probe-results/logs/wine-version.txt" 2>&1; then
  echo "[wine] 镜像自带 wine：$(cat "$GITHUB_WORKSPACE/probe-results/logs/wine-version.txt")"
else
  echo "::warning::镜像无 wine，apt 兜底安装（任务书指定形态，如实记录版本）"
  ok=""
  for i in 1 2 3; do
    if timeout -k 10 120 sudo apt-get update -qq; then
      if timeout -k 10 600 sudo apt-get install -y wine; then ok=1; break; fi
    fi
    echo "::warning::wine 安装第 $i 次失败/超时"; sleep 10
  done
  if [ -z "$ok" ]; then
    echo "::error::wine 三次安装重试均失败"
    exit 1
  fi
  wine --version | tee "$GITHUB_WORKSPACE/probe-results/logs/wine-version.txt"
fi
wine --version || { echo "::error::wine 仍不可用"; exit 1; }

exit 0
fi

# tailscale-dual.yml / linux-app / A. 记录 Tailscale 状态（linux-app）

if [ "$stage" = "s12" ]; then
set -uo pipefail
WS="$GITHUB_WORKSPACE"
mkdir -p "$WS/probe-results/logs"
if ! command -v tailscale >/dev/null 2>&1; then
  echo "::error::官方 action 已完成但 tailscale CLI 不在 PATH"
  exit 1
fi
tailscale version | tee "$WS/probe-results/logs/tailscale-version.txt"
A_IP=""
for i in $(seq 1 30); do
A_IP=$(tailscale ip -4 2>/dev/null | head -1 || true)
  [ -n "$A_IP" ] && break
  sleep 2
done
if [ -z "$A_IP" ]; then
  echo "::error::60s 内未取得 tailnet IPv4"
  tailscale status
  exit 1
fi
echo "[ts] linux-app 已入网：hostname=womic-dual-linux-${GITHUB_RUN_ID}-${GITHUB_RUN_ATTEMPT} ip=$A_IP"
tailscale status | tee "$WS/probe-results/logs/tailscale-status.txt"
echo "A_IP=$A_IP" >> "$GITHUB_ENV"

exit 0
fi

# tailscale-dual.yml / linux-app / A. 发现 windows-client 的 tailnet IP

if [ "$stage" = "s13" ]; then
set -uo pipefail
B_IP=""
for i in $(seq 1 24); do
  B_IP=$(tailscale status --json 2>/dev/null \
    | jq -r --arg host "womic-dual-win-${GITHUB_RUN_ID}-${GITHUB_RUN_ATTEMPT}" \
      '.Peer[] | select(.HostName == $host and .Online) | .TailscaleIPs[0]' \
    2>/dev/null | head -1 || true)
  if [ -n "$B_IP" ] && [ "$B_IP" != "null" ]; then break; fi
  echo "[discover] 第 $i 次：tailnet 内尚未看到本 run 的 windows-client 在线节点"
  sleep 5
done
if [ -z "$B_IP" ] || [ "$B_IP" = "null" ]; then
  echo "::error::120s 内未在 tailnet 发现 windows-client（B 应已先入网）"
  tailscale status
  exit 1
fi
echo "[discover] windows-client → $B_IP"
echo "B_IP=$B_IP" >> "$GITHUB_ENV"

exit 0
fi

# tailscale-dual.yml / linux-app / A. 选择 TCP 通路（slirp 直连优先，redir 兜底）

if [ "$stage" = "s14" ]; then
set -uo pipefail
WS="$GITHUB_WORKSPACE"
PATH_HOST="10.0.2.15"; PATH_PORT="$CONTROL_PORT"; PATH_DESC="slirp 直连 10.0.2.15:$CONTROL_PORT"
if python3 "$WS/.github/scripts/womic-hello-probe.py" \
    --host 10.0.2.15 --port "$CONTROL_PORT" \
    --timeout 30 --interval 2 --io-timeout 3 \
    2>&1 | tee "$WS/probe-results/logs/hello-direct.txt"; then
  echo "[path] 直连通路 OK：$PATH_DESC"
else
  echo "::warning::直连 10.0.2.15:$CONTROL_PORT 不通，走 redir 兜底"
  if ! python3 "$WS/.github/scripts/emu-redir.py" \
      --add "tcp:${REDIR_PORT}:${CONTROL_PORT}" --verify \
      2>&1 | tee "$WS/probe-results/logs/emu-redir-add.txt"; then
    echo "::error::redir 兜底失败（adb emu 与裸 console 两条通道均失败）"
    exit 1
  fi
  if ! python3 "$WS/.github/scripts/womic-hello-probe.py" \
      --host 127.0.0.1 --port "$REDIR_PORT" \
      --timeout 30 --interval 2 --io-timeout 3 \
      2>&1 | tee "$WS/probe-results/logs/hello-redir.txt"; then
    echo "::error::redir 后 127.0.0.1:$REDIR_PORT 仍不通"
    exit 1
  fi
  PATH_HOST="127.0.0.1"; PATH_PORT="$REDIR_PORT"; PATH_DESC="console redir 127.0.0.1:$REDIR_PORT→$CONTROL_PORT"
fi
echo "[path] socat 后端 = $PATH_DESC"
{
  echo "PATH_HOST=$PATH_HOST"
  echo "PATH_PORT=$PATH_PORT"
  echo "PATH_DESC=$PATH_DESC"
} >> "$GITHUB_ENV"

exit 0
fi

# tailscale-dual.yml / linux-app / A. wine 预检（真 app 本地诊断档，continue-on-error）

if [ "$stage" = "s15" ]; then
set -uo pipefail
WS="$GITHUB_WORKSPACE"
# pkill 自杀陷阱（VERIFICATION.md 教训 #4）：模式串会匹配自己所在
# shell —— 用 [.] 正则技巧让模式不匹配自身命令行。
pkill -f 'womic[.]exe' 2>/dev/null || true
sleep 1
set -o pipefail
python3 "$WS/client/verify/media_port_check.py" \
  --no-fake --wine --exe "$WS/dist/womic.exe" \
  --host "$PATH_HOST" --control-port "$PATH_PORT" \
  --media-port "$MEDIA_PORT" --timeout 60 \
  2>&1 | tee "$WS/probe-results/logs/wine-precheck.txt"
rc=$?
echo "[precheck] media_port_check --no-fake exit=$rc（0=三未知数全过）"
echo "WINEPRECHECK_RC=$rc" >> "$GITHUB_ENV"
pkill -f 'womic[.]exe' 2>/dev/null || true
exit 0

exit 0
fi

# tailscale-dual.yml / linux-app / A. 启动 UDP 中继（app → windows-client）

if [ "$stage" = "s16" ]; then
set -uo pipefail
WS="$GITHUB_WORKSPACE"
nohup python3 "$WS/.github/scripts/udp-relay.py" \
  --listen-port "$MEDIA_PORT" \
  --target "$B_IP" --target-port "$MEDIA_PORT" \
  --stats "$WS/probe-results/relay-stats.json" \
  > "$WS/probe-results/relay.log" 2>&1 &
echo $! > "$WS/probe-results/relay.pid"
sleep 2
echo "[relay] pid=$(cat "$WS/probe-results/relay.pid") listen=0.0.0.0:$MEDIA_PORT target=$B_IP:$MEDIA_PORT"
cat "$WS/probe-results/relay-stats.json" 2>/dev/null || echo "(统计文件尚未生成)"
kill -0 "$(cat "$WS/probe-results/relay.pid")" 2>/dev/null \
  || { echo "::error::UDP 中继进程退出"; cat "$WS/probe-results/relay.log"; exit 1; }

exit 0
fi

# tailscale-dual.yml / linux-app / A. 启动 socat TCP 中继（tailnet:8125 → app）

if [ "$stage" = "s17" ]; then
set -uo pipefail
WS="$GITHUB_WORKSPACE"
nohup socat \
  "TCP-LISTEN:${CONTROL_PORT},fork,reuseaddr" \
  "TCP:${PATH_HOST}:${PATH_PORT}" \
  > "$WS/probe-results/socat.log" 2>&1 &
echo $! > "$WS/probe-results/socat.pid"
sleep 2
echo "[socat] pid=$(cat "$WS/probe-results/socat.pid") listen=0.0.0.0:$CONTROL_PORT backend=$PATH_HOST:$PATH_PORT"
kill -0 "$(cat "$WS/probe-results/socat.pid")" 2>/dev/null \
  || { echo "::error::socat 进程退出"; cat "$WS/probe-results/socat.log"; exit 1; }
# 自检 socat 出口：走 127.0.0.1:8125 → socat → app（B 将走同一入口，
# 只是外层包 tailnet）。不过即失败——这正是 B 的就绪判据同款探针。
python3 "$WS/.github/scripts/womic-hello-probe.py" \
  --host 127.0.0.1 --port "$CONTROL_PORT" \
  --timeout 30 --interval 2 --io-timeout 3 \
  2>&1 | tee "$WS/probe-results/logs/hello-socat.txt" \
  || { echo "::error::socat 出口自检失败（hello 未通）"; exit 1; }

exit 0
fi

# tailscale-dual.yml / linux-app / A. 等待 windows-client 完成（gh 轮询 + 窗口兜底）

if [ "$stage" = "s19" ]; then
set -uo pipefail
WS="$GITHUB_WORKSPACE"
WIN="${WINDOW_SECONDS:-1200}"
case "$WIN" in (*[!0-9]*|"") WIN=1200;; esac
B_STATUS="unknown"; B_CONCLUSION=""
GH_OK=1
gh api "repos/$GH_REPO/actions/runs/$RUN_ID/jobs" --jq '.jobs | length' \
  >/dev/null 2>"$WS/probe-results/logs/gh-precheck.txt" || GH_OK=0
if [ "$GH_OK" != "1" ]; then
  echo "::warning::gh api 预检失败（见 gh-precheck.txt），退化为固定窗口 ${WIN}s"
  cat "$WS/probe-results/logs/gh-precheck.txt" || true
fi
if [ "$GH_OK" = "1" ]; then
  DEADLINE=$(( $(date +%s) + WIN ))
  while [ "$(date +%s)" -lt "$DEADLINE" ]; do
    J=$(gh api "repos/$GH_REPO/actions/runs/$RUN_ID/jobs" --jq \
      '.jobs[] | select(.name=="windows-client") | "\(.status) \(.conclusion)"' \
      2>/dev/null || echo "")
    echo "[wait] $(date -u +%H:%M:%S) windows-client: ${J:-<查询失败>}"
    S=$(printf '%s' "$J" | awk '{print $1}')
    if [ "$S" = "completed" ]; then
      B_STATUS="completed"
      B_CONCLUSION=$(printf '%s' "$J" | awk '{print $2}')
      break
    fi
    sleep 15
  done
  [ "$B_STATUS" = "completed" ] || B_STATUS="timeout"
else
  sleep "$WIN"
fi
echo "[wait] 结果：B_STATUS=$B_STATUS B_CONCLUSION=$B_CONCLUSION"
{
  echo "B_STATUS=$B_STATUS"
  echo "B_CONCLUSION=$B_CONCLUSION"
} >> "$GITHUB_ENV"

exit 0
fi

# tailscale-dual.yml / linux-app / A. 汇总判定

if [ "$stage" = "s20" ]; then
set -uo pipefail
WS="$GITHUB_WORKSPACE"
FORWARDED=-1
if [ -f "$WS/probe-results/relay-stats.json" ]; then
  FORWARDED=$(python3 -c \
    "import json;d=json.load(open('$WS/probe-results/relay-stats.json'));print(d.get('forwarded',0))" \
    2>/dev/null || echo -1)
fi
echo "[verdict] B_STATUS=$B_STATUS B_CONCLUSION=$B_CONCLUSION relay_forwarded=$FORWARDED"
cat "$WS/probe-results/relay-stats.json" 2>/dev/null || true
VERDICT=FAIL; REASON=""
if [ "$B_STATUS" = "completed" ]; then
  if [ "$B_CONCLUSION" = "success" ] && [ "$FORWARDED" -gt 0 ] 2>/dev/null; then
    VERDICT=PASS; REASON="B 成功且 UDP 中继转发 $FORWARDED 包"
  elif [ "$B_CONCLUSION" != "success" ]; then
    REASON="windows-client 结论=$B_CONCLUSION（详见 B 作业日志/dual-evidence-windows）"
  else
    REASON="B 成功但中继转发量=$FORWARDED（矛盾：B 收到音频必有转发）"
  fi
else
  if [ "$FORWARDED" -gt 0 ] 2>/dev/null; then
    VERDICT=PASS
    REASON="B 结论未知（gh 不可用/窗口超时 status=$B_STATUS），但中继转发 $FORWARDED 包"
    echo "::warning::$REASON —— B 的最终判据以 windows-client 作业为准"
  else
    REASON="B 结论未知（status=$B_STATUS）且中继转发量=$FORWARDED，无跨网流量证据"
  fi
fi
{
  echo "DUAL_VERDICT_A=$VERDICT"
  echo "DUAL_REASON_A=$REASON"
} >> "$GITHUB_ENV"
{
  echo "### linux-app（job A）判定：$VERDICT"
  echo ""
  echo "| 项 | 值 |"
  echo "|---|---|"
  echo "| A tailnet IP | ``$A_IP`` |"
  echo "| B tailnet IP | ``$B_IP`` |"
  echo "| TCP 通路 | ``$PATH_DESC`` |"
  echo "| wine 预检 exit | ``$WINEPRECHECK_RC``（诊断档，非硬判据） |"
  echo "| B 作业状态 | ``$B_STATUS`` / ``$B_CONCLUSION`` |"
  echo "| UDP 中继转发 | ``$FORWARDED`` 包 |"
  echo "| 理由 | $REASON |"
} >> "$GITHUB_STEP_SUMMARY"
if [ "$VERDICT" != "PASS" ]; then
  echo "::error::A 侧判定失败：$REASON"
  exit 1
fi
echo "[verdict] PASS：$REASON"
exit 0

exit 0
fi

# tailscale-dual.yml / linux-app / A. 收集证据

if [ "$stage" = "s21" ]; then
set -uo pipefail
WS="$GITHUB_WORKSPACE"
cd "$WS"
mkdir -p probe-results/logs
cp -f relay.log probe-results/ 2>/dev/null || true
cp -f socat.log probe-results/ 2>/dev/null || true
tailscale status --json > probe-results/logs/tailscale-status-final.json 2>/dev/null || true
timeout 20 adb devices > probe-results/logs/adb-devices.txt 2>&1 || true
timeout 20 adb logcat -d -t 60 > probe-results/logs/app-logcat.txt 2>&1 || true
timeout 20 adb shell "cat /proc/net/tcp | awk 'NR>1 {print \$2}' | grep -i ':1FBD' || echo '(8125 未监听)'" \
  > probe-results/logs/app-listen.txt 2>&1 || true
echo "[evidence] A 侧收证完成"
ls -la probe-results probe-results/logs | head -40

exit 0
fi

# tailscale-dual.yml / linux-app / A. 收尾（停中继）

if [ "$stage" = "s23" ]; then
set -uo pipefail
WS="$GITHUB_WORKSPACE"
for name in relay socat; do
  if [ -f "$WS/probe-results/$name.pid" ]; then
    PID=$(cat "$WS/probe-results/$name.pid")
    kill "$PID" 2>/dev/null && echo "[cleanup] 已停 $name (pid=$PID)" || true
  fi
done
echo "[cleanup] A 侧收尾完成"
exit 0

exit 0
fi
echo "unknown stage" >&2; exit 2
