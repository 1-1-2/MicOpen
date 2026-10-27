param([Parameter(Mandatory=$true)][string]$Stage)
$ErrorActionPreference = 'Stop'
# 产品验证逻辑来自旧流程；逐阶段启动独立 pwsh，保留 GITHUB_ENV 与退出码语义。
# tailscale-dual.yml / windows-client / 0. 环境自检（python / 系统信息 / 解锁产物）

if ($Stage -eq 's2') {
New-Item -ItemType Directory -Force -Path probe-results/logs | Out-Null
Get-ChildItem .\dist -Recurse -File | ForEach-Object {
  Unblock-File $_.FullName -ErrorAction SilentlyContinue
}
python --version
if ($LASTEXITCODE -ne 0) {
  Write-Host "::error::0: python 不可用"
  exit 1
}
"OS: $([System.Environment]::OSVersion.VersionString)" |
  Tee-Object probe-results/logs/0-env.txt
exit 0

exit 0
}

# tailscale-dual.yml / windows-client / B. 记录状态 + UDP 8126 入站放行

if ($Stage -eq 's4') {
$ErrorActionPreference = 'Stop'
$ts = "$env:ProgramFiles\Tailscale\tailscale.exe"
if (-not (Test-Path $ts)) {
  $cmd = Get-Command tailscale.exe -ErrorAction SilentlyContinue
  if ($cmd) { $ts = $cmd.Source }
}
if (-not (Test-Path $ts)) {
  Write-Host "::error::官方 action 已完成但 tailscale.exe 未就位"
  exit 1
}
& $ts version | Tee-Object probe-results/logs/tailscale-version.txt
$ip = ''
for ($i = 0; $i -lt 30 -and [string]::IsNullOrWhiteSpace($ip); $i++) {
  Start-Sleep -Seconds 2
  $cand = (& $ts ip -4 2>$null | Select-Object -First 1)
  if ($cand) { $ip = "$cand".Trim() }
}
if ([string]::IsNullOrWhiteSpace($ip)) {
  Write-Host "::error::60s 内未取得 tailnet IPv4"
  & $ts status
  exit 1
}
Write-Host "windows-client 已入网：hostname=womic-dual-win-$env:GITHUB_RUN_ID ip=$ip"
# 入站 UDP 8126（中继 → womic.exe 媒体口）：优先只放行 tailscale 适配器
$ruleName = 'womic-dual media udp'
Get-NetFirewallRule -Name $ruleName -ErrorAction SilentlyContinue | Remove-NetFirewallRule | Out-Null
$tsAlias = (Get-NetAdapter | Where-Object { $_.InterfaceDescription -like '*Tailscale*' } |
  Select-Object -First 1).Name
if ($tsAlias) {
  New-NetFirewallRule -Name $ruleName -DisplayName 'womic-dual media udp' `
    -InterfaceAlias $tsAlias -Direction Inbound -Protocol UDP `
    -LocalPort $env:MEDIA_PORT -Action Allow | Out-Null
  Write-Host "防火墙已限定到 tailscale 适配器: $tsAlias（UDP $env:MEDIA_PORT 入站）"
} else {
  Write-Host "::warning::未找到 tailscale 适配器，回退放行所有接口 UDP $env:MEDIA_PORT"
  New-NetFirewallRule -Name $ruleName -DisplayName 'womic-dual media udp' `
    -Direction Inbound -Protocol UDP -LocalPort $env:MEDIA_PORT -Action Allow | Out-Null
}
& $ts status | Tee-Object probe-results/logs/tailscale-status.txt
"B_IP=$ip" | Out-File -FilePath $env:GITHUB_ENV -Append
exit 0

exit 0
}

# tailscale-dual.yml / windows-client / B. 发现 linux-app 的 tailnet IP（长轮询）

if ($Stage -eq 's5') {
$ErrorActionPreference = 'Continue'
$ts = (Get-Command tailscale.exe -ErrorAction Stop).Source
$A_IP = ''
$deadline = (Get-Date).AddMinutes(45)
while ((Get-Date) -lt $deadline -and [string]::IsNullOrWhiteSpace($A_IP)) {
  Start-Sleep -Seconds 10
  try {
    $st = & $ts status --json 2>$null | ConvertFrom-Json
    if ($st -and $st.Peer) {
      foreach ($p in $st.Peer.PSObject.Properties) {
        if ("$($p.Value.HostName)" -eq "womic-dual-linux-$env:GITHUB_RUN_ID-$env:GITHUB_RUN_ATTEMPT" -and $p.Value.Online) {
          $A_IP = "$($p.Value.TailscaleIPs[0])"
          Write-Host "发现 linux-app 节点：$($p.Value.HostName) → $A_IP"
        }
      }
    }
  } catch {
    Write-Host "::warning::解析 tailscale status 失败：$_"
  }
  if ([string]::IsNullOrWhiteSpace($A_IP)) {
    $left = [int]($deadline - (Get-Date)).TotalMinutes
    Write-Host "wait: 尚未发现本 run 的 linux-app 在线节点（剩余约 $left 分钟）"
  }
}
if ([string]::IsNullOrWhiteSpace($A_IP)) {
  Write-Host "::error::45 分钟内未发现 linux-app 节点（A 侧构建/入网是否失败？）"
  & $ts status
  exit 1
}
"A_IP=$A_IP" | Out-File -FilePath $env:GITHUB_ENV -Append
exit 0

exit 0
}

# tailscale-dual.yml / windows-client / C1. 驱动安装器自助安装驱动（记录 UAC/非交互行为）

if ($Stage -eq 's6') {
$inst = '.\dist\womic-drvinst.exe'
$log = 'probe-results/logs/c1-install-client.txt'
if (-not (Test-Path $inst)) {
  Write-Host "::error::C1: 找不到 $inst（构建产物缺失）"
  Get-ChildItem .\dist -Force | Select-Object Name, Length | Out-String | Write-Host
  exit 1
}
& cmd /c "$inst /status > probe-results/logs/c1-status-pre.txt 2>&1"
"pre_status_exit=$LASTEXITCODE" | Out-File probe-results/logs/c1-status-pre.txt -Append
Get-Content probe-results/logs/c1-status-pre.txt -ErrorAction SilentlyContinue | Write-Host
& cmd /c "$inst /install /driver-pack `"$env:GITHUB_WORKSPACE\dist\womic-driver`" > $log 2>&1"
$rc = $LASTEXITCODE
Get-Content $log -ErrorAction SilentlyContinue | Write-Host
"installer_exit=$rc" | Out-File $log -Append
if ($rc -eq 0) {
  Write-Host "C1: 安装器退出码 0（是否真装上以 C2 系统状态为准）"
} else {
  Write-Host "::warning::C1: 安装器退出码 $rc —— 记录在案，由 C2/C3 兜底"
}
exit 0

exit 0
}

# tailscale-dual.yml / windows-client / C2. 验证驱动系统状态（SYS 落盘 + 注册表 InfPath）

if ($Stage -eq 's7') {
$sysOnDisk = Test-Path "$env:SystemRoot\System32\drivers\womic.sys"
$infPath = ""
$classKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e96c-e325-11ce-bfc1-08002be10318}'
Get-ChildItem $classKey -ErrorAction SilentlyContinue | ForEach-Object {
  $p = Get-ItemProperty -Path $_.PSPath -ErrorAction SilentlyContinue
  if ($p -and $p.MatchingDeviceId -match 'WOMic' -and $p.InfPath) {
    $infPath = $p.InfPath
  }
}
$state = if ($sysOnDisk -and $infPath) { 'ok' } else { 'bad' }
"sys_on_disk=$sysOnDisk inf_path=$infPath state=$state" |
  Tee-Object probe-results/logs/c2-system-state.txt
"install_state=$state" | Out-File -FilePath $env:GITHUB_ENV -Append
if ($state -eq 'ok') {
  Write-Host "C2: 驱动已就位（SYS 落盘 + InfPath=$infPath）"
} else {
  Write-Host "::warning::C2: 系统状态不完整（SYS=$sysOnDisk, InfPath='$infPath'），转 C3 兜底"
}
exit 0

exit 0
}

# tailscale-dual.yml / windows-client / C3. pnputil 兜底安装（仅 C2 未通过时）

if ($Stage -eq 's8') {
Write-Host "C3: 走兜底路径（pnputil /add-driver + 重试客户端安装）"
pnputil /add-driver "$env:GITHUB_WORKSPACE\dist\womic-driver\womic.inf" 2>&1 |
  Tee-Object -FilePath probe-results/logs/c3-pnputil.txt
& cmd /c ".\dist\womic.exe --install-driver --driver-pack `"$env:GITHUB_WORKSPACE\dist\womic-driver`" > probe-results/logs/c3-install-retry.txt 2>&1"
$rc = $LASTEXITCODE
Get-Content probe-results/logs/c3-install-retry.txt -ErrorAction SilentlyContinue | Write-Host
"retry_exit=$rc" | Out-File probe-results/logs/c3-install-retry.txt -Append
$sysOnDisk = Test-Path "$env:SystemRoot\System32\drivers\womic.sys"
$infPath = ""
$classKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e96c-e325-11ce-bfc1-08002be10318}'
Get-ChildItem $classKey -ErrorAction SilentlyContinue | ForEach-Object {
  $p = Get-ItemProperty -Path $_.PSPath -ErrorAction SilentlyContinue
  if ($p -and $p.MatchingDeviceId -match 'WOMic' -and $p.InfPath) { $infPath = $p.InfPath }
}
if (-not $sysOnDisk -or -not $infPath) {
  Write-Host "::error::C3: 兜底后驱动仍未就位（SYS=$sysOnDisk, InfPath='$infPath'）"
  exit 1
}
Write-Host "C3: 兜底安装成功（InfPath=$infPath）"
exit 0

exit 0
}

# tailscale-dual.yml / windows-client / D. 安装后 --status 退出码 0 + 驱动就绪（硬门禁）

if ($Stage -eq 's9') {
$exe = '.\dist\womic.exe'
$log = 'probe-results/logs/d-status-postinstall.txt'
if (-not (Test-Path $exe)) {
  Write-Host "::error::D: 找不到 $exe"
  exit 1
}
& cmd /c "$exe --status > $log 2>&1"
$rc = $LASTEXITCODE
$txt = if (Test-Path $log) { Get-Content $log -Raw -Encoding utf8 } else { '' }
Get-Content $log -ErrorAction SilentlyContinue | Write-Host
if ($rc -ne 0) {
  Write-Host "::error::D: 安装后 --status 退出码 $rc ≠ 0"
  exit 1
}
if ($txt -notmatch '已就绪') {
  Write-Host "::error::D: --status 未报告驱动已就绪"
  exit 1
}
if ($txt -match 'INF 路径\s*:\s*\(无\)') {
  Write-Host "::error::D: --status 的 INF 路径为空"
  exit 1
}
Write-Host "D: 驱动已就绪，INF 路径非空（退出码 0）"
exit 0

exit 0
}

# tailscale-dual.yml / windows-client / E. 跨网 TCP 通路就绪探活（hello）

if ($Stage -eq 's11') {
python "$env:GITHUB_WORKSPACE\.github\scripts\womic-hello-probe.py" `
  --host $env:A_IP --port $env:CONTROL_PORT `
  --timeout 180 --interval 5 --io-timeout 3 `
  2>&1 | Tee-Object -FilePath probe-results/logs/e-hello-readiness.txt
if ($LASTEXITCODE -ne 0) {
  Write-Host "::error::E: 跨网 TCP 通路未就绪（$env:A_IP`:$env:CONTROL_PORT hello 失败）"
  exit 1
}
Write-Host "E: 跨网 TCP 通路就绪（hello 收到 102 应答）"
exit 0

exit 0
}

# tailscale-dual.yml / windows-client / F. 启动 SYSTEM 录音计划任务（440Hz 取证，30s）

if ($Stage -eq 's12') {
$ws = $env:GITHUB_WORKSPACE
$py = (Get-Command python).Source
if (-not $py) {
  Write-Host "::error::F: 解析 python 绝对路径失败"
  exit 1
}
New-Item -ItemType Directory -Force -Path "$ws\probe-results\logs" | Out-Null
$bat = "$ws\probe-results\run_f.cmd"
$marker = "$ws\probe-results\rec_exit.txt"
if (Test-Path $marker) { Remove-Item $marker -Force }
# 不用 here-string（结束标记顶行首会截断 YAML 块标量）；
# 不用 @(...) 逗号后 + 拼接（会被解析成数组元素）——统一插值 + 反引号转义。
$batLines = @(
  '@echo off',
  'chcp 65001 >nul 2>&1',
  'set PYTHONIOENCODING=utf-8',
  "cd /d `"$ws`"",
  "`"$py`" .github\scripts\womic-record-probe.py --seconds 30 --out `"$ws\probe-results\record`" > `"$ws\probe-results\logs\f-record-probe.txt`" 2>&1",
  "echo PROBE_EXIT=%ERRORLEVEL% > `"$marker`""
)
$batLines | Set-Content -Path $bat -Encoding ascii
$xmlPath = "$ws\probe-results\WomicDualRec.xml"
$xmlLines = @(
  '<?xml version="1.0" encoding="UTF-16"?>',
  '<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">',
  '  <RegistrationInfo>',
  '    <Description>WO Mic dual 录音取证（SYSTEM/Session 0，脱离交互会话）</Description>',
  '    <URI>\WomicDualRec</URI>',
  '  </RegistrationInfo>',
  '  <Triggers>',
  '    <BootTrigger><Enabled>false</Enabled></BootTrigger>',
  '  </Triggers>',
  '  <Principals>',
  '    <Principal id="Author">',
  '      <UserId>S-1-5-18</UserId>',
  '      <RunLevel>HighestAvailable</RunLevel>',
  '    </Principal>',
  '  </Principals>',
  '  <Settings>',
  '    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>',
  '    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>',
  '    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>',
  '    <AllowHardTerminate>true</AllowHardTerminate>',
  '    <StartWhenAvailable>true</StartWhenAvailable>',
  '    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>',
  '    <IdleSettings>',
  '      <StopOnIdleEnd>false</StopOnIdleEnd>',
  '      <RestartOnIdle>false</RestartOnIdle>',
  '    </IdleSettings>',
  '    <AllowStartOnDemand>true</AllowStartOnDemand>',
  '    <Enabled>true</Enabled>',
  '    <Hidden>false</Hidden>',
  '    <RunOnlyIfIdle>false</RunOnlyIfIdle>',
  '    <WakeToRun>false</WakeToRun>',
  '    <ExecutionTimeLimit>PT10M</ExecutionTimeLimit>',
  '    <Priority>4</Priority>',
  '  </Settings>',
  '  <Actions Context="Author">',
  '    <Exec>',
  '      <Command>cmd.exe</Command>',
  "      <Arguments>/c `"$bat`"</Arguments>",
  "      <WorkingDirectory>$ws</WorkingDirectory>",
  '    </Exec>',
  '  </Actions>',
  '</Task>'
)
[IO.File]::WriteAllText($xmlPath, ($xmlLines -join "`r`n"), [Text.Encoding]::Unicode)
# $ran=false 时不删（创建/触发失败要清掉定义，别留孤儿任务）；
# 成功触发后定义留给 H 步轮询完 marker 再删（windows-e2e f 步同款次序）
$ran = $false
try {
  & schtasks /create /tn WomicDualRec /xml $xmlPath /f 2>&1 | Out-String | Write-Host
  if ($LASTEXITCODE -ne 0) {
    Write-Host "::error::F: 创建计划任务 WomicDualRec 失败"
    exit 1
  }
  & schtasks /run /tn WomicDualRec 2>&1 | Out-String | Write-Host
  if ($LASTEXITCODE -ne 0) {
    Write-Host "::error::F: 触发计划任务 WomicDualRec 失败"
    exit 1
  }
  $ran = $true
  Write-Host "F: 录音任务已触发（30s 窗口），等 G 步启动 womic.exe"
} finally {
  if (-not $ran) {
    & schtasks /delete /tn WomicDualRec /f 2>&1 | Out-Null
  }
}
exit 0

exit 0
}

# tailscale-dual.yml / windows-client / G. 启动真 womic.exe（跨网 + 固定媒体端口 8126）

if ($Stage -eq 's13') {
$exe = '.\dist\womic.exe'
$log = 'probe-results/logs/g-womic-run.txt'
if (-not (Test-Path $exe)) {
  Write-Host "::error::G: 找不到 $exe"
  Get-ChildItem .\dist -Force | Select-Object Name, Length | Out-String | Write-Host
  exit 1
}
Start-Sleep -Seconds 5
& cmd /c "$exe --host $env:A_IP --port $env:CONTROL_PORT --media-port $env:MEDIA_PORT --connect --no-gui > $log 2>&1"
$rc = $LASTEXITCODE
$txt = if (Test-Path $log) { Get-Content $log -Raw -Encoding utf8 } else { '' }
Get-Content $log -ErrorAction SilentlyContinue | Write-Host
"womic_exit=$rc" | Out-File probe-results/logs/g-womic-run.txt -Append
# OK 行契约（client/src/cli.c 无窗口冒烟模式）：
#   OK  帧=N 字节=M 注入=K 电平=x dB=y 解码=Opus 设备=就绪|未就绪 媒体端口=P(固定|随机)
$m = [regex]::Match($txt, 'OK\s+帧=(\d+)\s+字节=(\d+)\s+注入=(\d+).*?设备=(\S+)\s+媒体端口=(\d+)\((固定|随机)\)')
if ($rc -ne 0 -or -not $m.Success) {
  Write-Host "::error::G: womic.exe 退出码=$rc 且未解析到 OK 行（跨网链路失败？）"
  exit 1
}
$frames = [int]$m.Groups[1].Value
$bytes = [int]$m.Groups[2].Value
$injected = [int]$m.Groups[3].Value
$device = $m.Groups[4].Value
$mport = [int]$m.Groups[5].Value
$mode = $m.Groups[6].Value
Write-Host "G: 帧=$frames 字节=$bytes 注入=$injected 设备=$device 媒体端口=$mport($mode)"
if ($frames -le 0 -or $bytes -le 0) {
  Write-Host "::error::G: 帧/字节为 0——没收到 UDP 媒体流"
  exit 1
}
if ($mport -ne [int]$env:MEDIA_PORT -or $mode -ne '固定') {
  Write-Host "::error::G: 媒体端口 $mport($mode) ≠ 期望 $env:MEDIA_PORT(固定)"
  exit 1
}
if ($device -ne '就绪') {
  Write-Host "::error::G: OK 行报告设备未就绪（驱动运行时异常）"
  exit 1
}
if ($injected -le 0) {
  Write-Host "::error::G: 注入字节为 0——解码音没灌进驱动"
  exit 1
}
# ⚠ 必须逐行字符串直排 Out-File：pwsh 里 `{ "k=$v" } | Out-File` 会把
# scriptblock 当对象 ToString（写进去的是带引号/不插值的源码文本，
# run 40082681621 实证：GITHUB_ENV 落下字面量 "womic_frames=$frames"，
# I 步 $env:womic_frames 读空 → 汇总判定误败）。同 workflow 的
# B_IP/install_state/record_result 都是这个直排写法，一直正常。
"womic_frames=$frames" | Out-File -FilePath $env:GITHUB_ENV -Append
"womic_bytes=$bytes" | Out-File -FilePath $env:GITHUB_ENV -Append
"womic_injected=$injected" | Out-File -FilePath $env:GITHUB_ENV -Append
"womic_device=$device" | Out-File -FilePath $env:GITHUB_ENV -Append
"womic_mport=$mport($mode)" | Out-File -FilePath $env:GITHUB_ENV -Append
exit 0

exit 0
}

# tailscale-dual.yml / windows-client / H. 读取录音取证判据（waveIn 440Hz）

if ($Stage -eq 's14') {
$ws = $env:GITHUB_WORKSPACE
$marker = "$ws\probe-results\rec_exit.txt"
try {
  $deadline = (Get-Date).AddMinutes(4)
  while (-not (Test-Path $marker) -and (Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 3
  }
  if (-not (Test-Path $marker)) {
    Write-Host "::error::H: 录音探针 4 分钟超时（marker 未出现）"
    & schtasks /query /tn WomicDualRec /fo LIST /v 2>&1 | Out-String | Write-Host
    exit 1
  }
  $m = [regex]::Match((Get-Content $marker -Raw), 'PROBE_EXIT=(\d+)')
  $probeExit = if ($m.Success) { [int]$m.Groups[1].Value } else { -1 }
  $log = "$ws\probe-results\logs\f-record-probe.txt"
  $txt = if (Test-Path $log) { Get-Content $log -Raw -Encoding utf8 } else { '' }
  Get-Content $log -ErrorAction SilentlyContinue | Write-Host
  if ($probeExit -eq 0 -and $txt -match 'RESULT=PASS') {
    Write-Host "H: 录音取证 PASS（waveIn 录到 440Hz 调试音，设备/句柄/RMS/主峰/SNR 全过）"
    "record_result=PASS" | Out-File -FilePath $env:GITHUB_ENV -Append
  } else {
    Write-Host "::error::H: 录音取证未 PASS（probe_exit=$probeExit）——见 f-record-probe.txt 与 record/summary.json"
    "record_result=FAIL" | Out-File -FilePath $env:GITHUB_ENV -Append
    exit 1
  }
} finally {
  # 失败路径也删：避免残留任务定义（成功路径此刻探针已跑完，删除安全）
  & schtasks /delete /tn WomicDualRec /f 2>&1 | Out-Null
}
exit 0

exit 0
}

# tailscale-dual.yml / windows-client / I. 汇总判定（双证据链）

if ($Stage -eq 's15') {
# G/H 步通过才会到这里（它们失败即作业失败），env 必然已写；
# 仍用 TryParse 防御空值/异常值，不让判定步自己崩。
$frames = 0; $injected = 0
[void][int]::TryParse("$env:womic_frames", [ref]$frames)
[void][int]::TryParse("$env:womic_injected", [ref]$injected)
$ok = ($env:record_result -eq 'PASS') -and ($frames -gt 0) -and ($injected -gt 0)
$sumPath = "$env:GITHUB_WORKSPACE\probe-results\record\summary.json"
$rms = ''; $peak = ''; $snr = ''
if (Test-Path $sumPath) {
  try {
    $s = Get-Content $sumPath -Raw -Encoding utf8 | ConvertFrom-Json
    $rms = "$($s.detail.analysis.rms)"
    $peak = "$($s.detail.analysis.peak_freq)Hz"
    $snr = "$($s.detail.analysis.snr)"
  } catch { }
}
@(
  "### windows-client（job B）判定：$(if ($ok) { 'PASS' } else { 'FAIL' })",
  "",
  "| 项 | 值 |",
  "|---|---|",
  "| A tailnet IP | ``$env:A_IP`` |",
  "| B tailnet IP | ``$env:B_IP`` |",
  "| womic OK 行 | 帧=$env:womic_frames 字节=$env:womic_bytes 注入=$env:womic_injected 设备=$env:womic_device 端口=$env:womic_mport |",
  "| 录音判据 | RESULT=$env:record_result |",
  "| 录音 RMS / 主峰 / SNR | $rms / $peak / $snr |",
  "",
  "音频原始文件：artifact ``dual-evidence-windows`` → probe-results/record/capture.wav"
) | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
if (-not $ok) {
  Write-Host "::error::I: 双证据链不完整（record=$env:record_result frames=$env:womic_frames injected=$env:womic_injected）"
  exit 1
}
Write-Host "I: PASS —— 真 womic.exe ↔ 真 app 跨网全链路 + 440Hz 音频取证"
exit 0

exit 0
}

# tailscale-dual.yml / windows-client / 收尾（移除媒体防火墙规则）

if ($Stage -eq 's17') {
Get-NetFirewallRule -Name 'womic-dual media udp' -ErrorAction SilentlyContinue |
  Remove-NetFirewallRule -ErrorAction SilentlyContinue | Out-Null
Write-Host "媒体 UDP 防火墙规则已清理"
exit 0

exit 0
}
throw 'unknown stage'
