#!/usr/bin/env bash
# 从 windows-e2e 构建步骤迁移：保留 apt 镜像与分层硬超时，避免静默卡死。
# 公开产物必须强制空驱动包；真实驱动在 Windows 验证机现场获取，不经 artifact 中转。
set -euo pipefail
cd "$GITHUB_WORKSPACE"
echo 'Acquire::http::Timeout "20";' | sudo tee /etc/apt/apt.conf.d/99timeout >/dev/null
echo 'Acquire::https::Timeout "20";' | sudo tee -a /etc/apt/apt.conf.d/99timeout >/dev/null
echo 'Acquire::Retries "2";' | sudo tee -a /etc/apt/apt.conf.d/99timeout >/dev/null
echo 'Acquire::ForceIPv4 "true";' | sudo tee -a /etc/apt/apt.conf.d/99timeout >/dev/null
# 直接换源到 Azure 内网镜像，不做探活。
# GitHub 托管的 runner 本身跑在 Azure 上，走内网镜像比公网稳且快；
# 公网链路 2026-10-23 晚多次抖动（同刻不同 runner：一个 28s 过、
# 一个静默挂 10 分钟）。
#
# 关键（run 39981519800 实证）：GitHub 的 ubuntu-24.04 镜像里
# sources 写的是 "URIs: mirror+file:/etc/apt/apt-mirrors.txt"，
# 主机名不在 sources 里，而在 apt-mirrors.txt 这个候选列表里——
# 只 sed sources 是空操作。两条路都处理：
#   1) 有 apt-mirrors.txt → 备份后收敛成 Azure 一行；
#   2) 没有该文件（老镜像）→ 直接改 sources 里的主机名。
# 保险 2：把 ubuntu 的 deb822 sources 的 URIs 行直接钉死到 Azure，
# 绕过 mirror+file 选择逻辑（文件不存在/格式变化都不影响）。
for f in /etc/apt/sources.list.d/*ubuntu*.sources; do
  [ -f "$f" ] || continue
  sudo sed -i 's|^URIs: .*|URIs: http://azure.archive.ubuntu.com/ubuntu/|' "$f"
  echo "改写 $f"
done
if [ -f /etc/apt/apt-mirrors.txt ]; then
  sudo cp /etc/apt/apt-mirrors.txt "$RUNNER_TEMP/apt-mirrors.txt.original"
  echo 'http://azure.archive.ubuntu.com/ubuntu/' | sudo tee /etc/apt/apt-mirrors.txt >/dev/null
  echo "===== apt-mirrors.txt（已收敛，原始备份位于 RUNNER_TEMP）====="
  cat /etc/apt/apt-mirrors.txt
else
  sudo sed -i \
    -e 's|http://archive.ubuntu.com/ubuntu|http://azure.archive.ubuntu.com/ubuntu|g' \
    -e 's|https://archive.ubuntu.com/ubuntu|https://azure.archive.ubuntu.com/ubuntu|g' \
    -e 's|http://security.ubuntu.com/ubuntu|http://azure.archive.ubuntu.com/ubuntu|g' \
    -e 's|https://security.ubuntu.com/ubuntu|https://azure.archive.ubuntu.com/ubuntu|g' \
    /etc/apt/sources.list /etc/apt/sources.list.d/*.sources 2>/dev/null || true
  echo "===== 无 apt-mirrors.txt，已直接改 sources 主机名 ====="
fi
echo "===== sources 现状 ====="
grep -rhE '^(deb |URIs:)' /etc/apt/sources.list /etc/apt/sources.list.d/*.sources 2>/dev/null | head -6 || true
ok=""
# timeout -k：TERM 后 10s 再 KILL。apt 有时卡在 D 状态不响应 TERM，
# 只给 TERM 的 timeout 会一直等下去（本轮 7m40s 的另一种解释）。
# 2026-10-27 run 40480079120：Azure 同一源连续 3×240s 无进展。
# 首选仍保留历史上可靠的 Azure，失败后换官方 archive，而非反复请求同一镜像。
for i in 1 2 3; do
  case "$i" in
    1) mirror=http://azure.archive.ubuntu.com/ubuntu/ ;;
    2) mirror=http://archive.ubuntu.com/ubuntu/ ;;
    3) mirror=https://archive.ubuntu.com/ubuntu/ ;;
  esac
  for f in /etc/apt/sources.list.d/*ubuntu*.sources; do
    [ -f "$f" ] || continue
    sudo sed -i "s|^URIs: .*|URIs: $mirror|" "$f"
  done
  if [ -f /etc/apt/apt-mirrors.txt ]; then
    echo "$mirror" | sudo tee /etc/apt/apt-mirrors.txt >/dev/null
  else
    # 旧式 sources.list 也必须随重试切换，不能只修 deb822 镜像。
    if [ -f /etc/apt/sources.list ]; then
      sudo sed -i -E "s|https?://(azure[.]archive|archive|security)[.]ubuntu[.]com/ubuntu/?|$mirror|g" /etc/apt/sources.list
    fi
  fi
  echo "===== apt 尝试 $i/3：update ($mirror) ====="
  if timeout -k 10 120 sudo apt-get update; then
    echo "===== apt 尝试 $i/3：install MinGW POSIX toolchain ====="
    if timeout -k 10 240 sudo apt-get install -y --no-install-recommends gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64; then
      ok=1; break
    fi
    echo "::warning::apt 第 $i 次 install 失败/超时"
  else
    echo "::warning::apt 第 $i 次 update 失败/超时（exit=$?）"
  fi
  sleep 10
done
if [ -z "$ok" ]; then
  echo "::error::apt 三次重试均失败"
  echo "连通性探针（archive.ubuntu.com）："
  curl -sS -m 5 -o /dev/null -w 'HTTP %{http_code} in %{time_total}s\n' http://archive.ubuntu.com/ubuntu/ || true
  exit 1
fi
x86_64-w64-mingw32-gcc --version | head -1
x86_64-w64-mingw32-objdump --version | head -1
if ! command -v 7z >/dev/null 2>&1; then
  # p7zip-full（老 7z）优先，装不上再试新 7zip 包；同样带硬超时
  for i in 1 2 3; do
    if timeout -k 10 180 sudo apt-get install -y -qq p7zip-full || timeout -k 10 180 sudo apt-get install -y -qq 7zip; then break; fi
    echo "::warning::7z 安装第 $i 次失败，10s 后重试"; sleep 10
  done
fi
7z | head -2 || { echo "::error::7z 不可用"; exit 1; }

python3 .github/scripts/ci.py run --seconds 900 -- bash client/tools/build_opus.sh
(cd client && python3 tools/pack_driver.py --empty && bash build.sh)
bash client/monitor/build.sh
bash client/tools/build_adb_tests.sh
python3 .github/scripts/ci.py stage --kind windows
