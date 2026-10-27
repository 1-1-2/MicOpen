param([Parameter(Mandatory=$true)][string]$Stage)
$ErrorActionPreference = 'Stop'
# 产品验证逻辑来自旧流程；逐阶段启动独立 pwsh，保留 GITHUB_ENV 与退出码语义。
# windows-e2e.yml / verify / 0. 环境自检（python / 系统信息）

if ($Stage -eq 's2') {
New-Item -ItemType Directory -Force -Path probe-results/logs | Out-Null
# artifact 下载的文件可能带 Zone.Identifier（MOTW），SmartScreen 会
# 拦会话 0 里的静默执行；统一解锁，排除"文件在但跑不起来"类干扰。
Get-ChildItem .\dist -Recurse -File | ForEach-Object {
  Unblock-File $_.FullName -ErrorAction SilentlyContinue
}
python --version
if ($LASTEXITCODE -ne 0) { Write-Host "::error::0: python 不可用"; exit 1 }
"OS: $([System.Environment]::OSVersion.VersionString)" |
  Tee-Object probe-results/logs/0-env.txt
# 显式退出码 0：pwsh 部分版本会把脚本末尾残留的 $LASTEXITCODE
# 传播为进程退出码（Round 2 实测踩坑），成功路径一律显式声明。
exit 0

exit 0
}

# windows-e2e.yml / verify / a. test_drvpkg.exe 驱动包解压自检（须 9/9）

if ($Stage -eq 's4') {
$out = & .\dist\test_drvpkg.exe 2>&1
$rc = $LASTEXITCODE
$out | Tee-Object -FilePath probe-results/logs/a-test_drvpkg.txt
$okN = ($out | Where-Object { $_ -match '^ok  : arch=' }).Count
"exit=$rc ok_lines=$okN" | Out-File probe-results/logs/a-test_drvpkg.txt -Append
if ($rc -ne 0 -or $okN -ne 9 -or ($out -join "`n") -notmatch 'RESULT=PASS') {
  Write-Host "::error::a: test_drvpkg 未 9/9 PASS（exit=$rc, ok=$okN）"
  "ITEM_A=FAIL" | Out-File -FilePath $env:GITHUB_ENV -Append
  exit 1
}
"ITEM_A=PASS" | Out-File -FilePath $env:GITHUB_ENV -Append
exit 0

exit 0
}

# windows-e2e.yml / verify / b. womic.exe --help CLI 说明书完整性

if ($Stage -eq 's5') {
$exe = '.\dist\womic.exe'
$log = 'probe-results/logs/b-help.txt'
if (-not (Test-Path $exe)) {
  Write-Host "::error::b: 找不到 $exe"
  Get-ChildItem .\dist -Force | Select-Object Name, Length | Out-String | Write-Host
  "ITEM_B=FAIL" | Out-File -FilePath $env:GITHUB_ENV -Append
  exit 1
}
& cmd /c "$exe --help > $log 2>&1"
$rc = $LASTEXITCODE
$txt = if (Test-Path $log) { Get-Content $log -Raw -Encoding utf8 } else { '' }
$miss = @()
foreach ($opt in @('--status', '--install-driver', '--driver-pack', '--connect')) {
  if ($txt -notmatch [regex]::Escape($opt)) { $miss += $opt }
}
if ($rc -ne 0 -or $miss.Count -gt 0 -or [string]::IsNullOrWhiteSpace($txt)) {
  Write-Host "::error::b: --help 退出码=$rc，缺少选项: $($miss -join ' ')，输出 $($txt.Length) 字符"
  # 失败诊断：目录列举 + 文件哈希 + 去掉重定向再直跑一次看原始返回
  Get-ChildItem .\dist -Force | Select-Object Name, Length | Out-String | Write-Host
  "sha256=$((Get-FileHash $exe -Algorithm SHA256).Hash)" | Write-Host
  $raw = & cmd /c "$exe --help" 2>&1
  "raw_rc=$LASTEXITCODE raw_lines=$($raw.Count)" | Write-Host
  "ITEM_B=FAIL" | Out-File -FilePath $env:GITHUB_ENV -Append
  exit 1
}
Write-Host "b: 帮助文本完整（退出码 $rc，$($txt.Length) 字符）"
"ITEM_B=PASS" | Out-File -FilePath $env:GITHUB_ENV -Append
exit 0

exit 0
}

# windows-e2e.yml / verify / c. 未装驱动 --status 退出码 3 + 架构信息

if ($Stage -eq 's6') {
$exe = '.\dist\womic.exe'
$log = 'probe-results/logs/c-status-preinstall.txt'
if (-not (Test-Path $exe)) {
  Write-Host "::error::c: 找不到 $exe"
  Get-ChildItem .\dist -Force | Select-Object Name, Length | Out-String | Write-Host
  "ITEM_C=FAIL" | Out-File -FilePath $env:GITHUB_ENV -Append
  exit 1
}
& cmd /c "$exe --status > $log 2>&1"
$rc = $LASTEXITCODE
$txt = if (Test-Path $log) { Get-Content $log -Raw -Encoding utf8 } else { '' }
if ($rc -ne 3) {
  Write-Host "::error::c: 未装驱动时 --status 退出码 $rc ≠ 3"
  Get-Content $log -ErrorAction SilentlyContinue | Out-String | Write-Host
  "ITEM_C=FAIL" | Out-File -FilePath $env:GITHUB_ENV -Append
  exit 1
}
if ($txt -notmatch '架构') {
  Write-Host "::error::c: --status 输出缺少架构信息"
  Get-Content $log -ErrorAction SilentlyContinue | Out-String | Write-Host
  "ITEM_C=FAIL" | Out-File -FilePath $env:GITHUB_ENV -Append
  exit 1
}
Write-Host "c: 退出码 3 + 架构信息在（符合驱动未装契约）"
"ITEM_C=PASS" | Out-File -FilePath $env:GITHUB_ENV -Append
exit 0

exit 0
}

# windows-e2e.yml / verify / d1. 驱动安装器自助安装驱动（记录 UAC/非交互行为）

if ($Stage -eq 's8') {
$inst = '.\dist\womic-drvinst.exe'
$log = 'probe-results/logs/d1-install-client.txt'
# 安装器是控制台子系统 + requireAdministrator，用 cmd /c 包一层调用：
# 既能把 stdio 稳定重定向到日志文件，也能正确回传退出码（见 b 步注释）。
if (-not (Test-Path $inst)) {
  Write-Host "::error::d1: 找不到 $inst（构建产物缺失）"
  Get-ChildItem .\dist -Force | Select-Object Name, Length | Out-String | Write-Host
  "client_install_exit=missing_installer" | Out-File -FilePath $env:GITHUB_ENV -Append
  exit 1
}
# 0) 装之前先照一眼契约：安装器 /status 与应用 --status 语义必须一致
#    （0=已装 / 3=未装）。这里只记录不断言，让 c 步与安装器自身平行印证。
& cmd /c "$inst /status > probe-results/logs/d1-status-pre.txt 2>&1"
"pre_status_exit=$LASTEXITCODE" | Out-File probe-results/logs/d1-status-pre.txt -Append
Get-Content probe-results/logs/d1-status-pre.txt -ErrorAction SilentlyContinue | Write-Host
# 1) 真正的安装
& cmd /c "$inst /install /driver-pack `"$env:GITHUB_WORKSPACE\dist\womic-driver`" > $log 2>&1"
$rc = $LASTEXITCODE
Get-Content $log -ErrorAction SilentlyContinue | Write-Host
"installer_exit=$rc" | Out-File $log -Append
"client_install_exit=$rc" | Out-File -FilePath $env:GITHUB_ENV -Append
if ($rc -eq 0) {
  Write-Host "d1: 安装器退出码 0（是否真装上以 d2 系统状态为准）"
} else {
  Write-Host "::warning::d1: 安装器退出码 $rc —— 保留首错，由 d2/d3 核实系统状态与兜底结果"
  Write-Host "::warning::d1: 本流程已经显式提供外部驱动目录；空包或权限错误仍须排查，不先验归为预期降级"
}
# d1 是记录型步骤（continue-on-error）：无论安装退出码如何，
# 本步骤本身以"已记录"为成功，显式 exit 0。
exit 0

exit 0
}

# windows-e2e.yml / verify / d2. 验证驱动系统状态（SYS 落盘 + 注册表 InfPath）

if ($Stage -eq 's9') {
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
  Tee-Object probe-results/logs/d2-system-state.txt
"install_state=$state" | Out-File -FilePath $env:GITHUB_ENV -Append
if ($state -eq 'ok') {
  Write-Host "d2: 驱动已就位（SYS 落盘 + InfPath=$infPath）"
} else {
  Write-Host "::warning::d2: 系统状态不完整（SYS=$sysOnDisk, InfPath='$infPath'），转 d3 兜底"
}

exit 0
}

# windows-e2e.yml / verify / d3. pnputil 兜底安装（仅 d2 未通过时）

if ($Stage -eq 's10') {
Write-Host "d3: 走兜底路径（pnputil /add-driver + 重试客户端安装）"
pnputil /add-driver "$env:GITHUB_WORKSPACE\dist\womic-driver\womic.inf" 2>&1 |
  Tee-Object -FilePath probe-results/logs/d3-pnputil.txt
# cmd /c 包装：GUI 子系统程序，& 直调在真机 runner 上不可靠（见 b 注释）
& cmd /c ".\dist\womic.exe --install-driver --driver-pack `"$env:GITHUB_WORKSPACE\dist\womic-driver`" > probe-results/logs/d3-install-retry.txt 2>&1"
$rc = $LASTEXITCODE
Get-Content probe-results/logs/d3-install-retry.txt -ErrorAction SilentlyContinue | Write-Host
"retry_exit=$rc" | Out-File probe-results/logs/d3-install-retry.txt -Append
$sysOnDisk = Test-Path "$env:SystemRoot\System32\drivers\womic.sys"
$infPath = ""
$classKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e96c-e325-11ce-bfc1-08002be10318}'
Get-ChildItem $classKey -ErrorAction SilentlyContinue | ForEach-Object {
  $p = Get-ItemProperty -Path $_.PSPath -ErrorAction SilentlyContinue
  if ($p -and $p.MatchingDeviceId -match 'WOMic' -and $p.InfPath) { $infPath = $p.InfPath }
}
if (-not $sysOnDisk -or -not $infPath) {
  Write-Host "::error::d3: 兜底后驱动仍未就位（SYS=$sysOnDisk, InfPath='$infPath'）"
  "ITEM_D=FAIL" | Out-File -FilePath $env:GITHUB_ENV -Append
  exit 1
}
Write-Host "d3: 兜底安装成功（InfPath=$infPath）"
"ITEM_D=PASS" | Out-File -FilePath $env:GITHUB_ENV -Append
exit 0

exit 0
}

# windows-e2e.yml / verify / e. 安装后 --status 退出码 0 + 驱动就绪

if ($Stage -eq 's11') {
$exe = '.\dist\womic.exe'
$log = 'probe-results/logs/e-status-postinstall.txt'
if (-not (Test-Path $exe)) {
  Write-Host "::error::e: 找不到 $exe"
  "ITEM_E=FAIL" | Out-File -FilePath $env:GITHUB_ENV -Append
  exit 1
}
& cmd /c "$exe --status > $log 2>&1"
$rc = $LASTEXITCODE
$txt = if (Test-Path $log) { Get-Content $log -Raw -Encoding utf8 } else { '' }
Get-Content $log -ErrorAction SilentlyContinue | Write-Host
if ($rc -ne 0) {
  Write-Host "::error::e: 安装后 --status 退出码 $rc ≠ 0"
  "ITEM_E=FAIL" | Out-File -FilePath $env:GITHUB_ENV -Append
  exit 1
}
if ($txt -notmatch '已就绪') {
  Write-Host "::error::e: --status 未报告驱动已就绪"
  "ITEM_E=FAIL" | Out-File -FilePath $env:GITHUB_ENV -Append
  exit 1
}
if ($txt -match 'INF 路径\s*:\s*\(无\)') {
  Write-Host "::error::e: --status 的 INF 路径为空"
  "ITEM_E=FAIL" | Out-File -FilePath $env:GITHUB_ENV -Append
  exit 1
}
Write-Host "e: 驱动已就绪，INF 路径非空（退出码 0）"
"ITEM_E=PASS" | Out-File -FilePath $env:GITHUB_ENV -Append
exit 0

exit 0
}

# windows-e2e.yml / verify / f. 端到端灌音验证（SYSTEM 计划任务跑 e2e_probe，须 RESULT=PASS）

if ($Stage -eq 's12') {
$ws = $env:GITHUB_WORKSPACE
# 计划任务的 cmd 环境不继承 Actions 进程 PATH，python 必须写绝对路径
$py = (Get-Command python).Source
if (-not $py) {
  Write-Host "::error::f: 解析 python 绝对路径失败"
  "ITEM_F=FAIL" | Out-File -FilePath $env:GITHUB_ENV -Append
  exit 1
}
# 预建收证目录：避免 bat 里重定向因目录不存在而失败
New-Item -ItemType Directory -Force -Path "$ws\probe-results\logs" | Out-Null
$bat = "$ws\probe-results\run_f.cmd"
$marker = "$ws\probe-results\f_exit.txt"
if (Test-Path $marker) { Remove-Item $marker -Force }
# 不用 here-string（结束标记顶行首会截断 YAML 块标量）；
# 不用 @(...) 逗号后 + 拼接（会被解析成数组元素）——统一插值 + 反引号转义。
$batLines = @(
  '@echo off',
  'chcp 65001 >nul 2>&1',
  'set PYTHONIOENCODING=utf-8',
  "cd /d `"$ws`"",
  "`"$py`" client\tools\e2e_probe.py --seconds 3 --out `"$ws\probe-results\probe`" > `"$ws\probe-results\logs\f-e2e-probe.txt`" 2>&1",
  "echo PROBE_EXIT=%ERRORLEVEL% > `"$marker`""
)
$batLines | Set-Content -Path $bat -Encoding ascii
# 计划任务 XML：schtasks /create /xml 要求 XML 文件 UTF-16 编码
$xmlPath = "$ws\probe-results\WomicE2EF.xml"
$xmlLines = @(
  '<?xml version="1.0" encoding="UTF-16"?>',
  '<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">',
  '  <RegistrationInfo>',
  '    <Description>WOMic E2E f 探针（SYSTEM/Session 0，脱离交互会话）</Description>',
  '    <URI>\WomicE2EF</URI>',
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
try {
  & schtasks /create /tn WomicE2EF /xml $xmlPath /f 2>&1 | Out-String | Write-Host
  if ($LASTEXITCODE -ne 0) {
    Write-Host "::error::f: 创建计划任务 WomicE2EF 失败"
    "ITEM_F=FAIL" | Out-File -FilePath $env:GITHUB_ENV -Append
    exit 1
  }
  & schtasks /run /tn WomicE2EF 2>&1 | Out-String | Write-Host
  if ($LASTEXITCODE -ne 0) {
    Write-Host "::error::f: 触发计划任务 WomicE2EF 失败"
    "ITEM_F=FAIL" | Out-File -FilePath $env:GITHUB_ENV -Append
    exit 1
  }
  # 轮询 marker（bat 末尾写），最多 4 分钟，每 3 秒一次
  $deadline = (Get-Date).AddMinutes(4)
  while (-not (Test-Path $marker) -and (Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 3
  }
  if (-not (Test-Path $marker)) {
    Write-Host "::error::f: 计划任务探针 4 分钟超时（marker 未出现）"
    & schtasks /query /tn WomicE2EF /fo LIST /v 2>&1 | Out-String | Write-Host
    "ITEM_F=FAIL" | Out-File -FilePath $env:GITHUB_ENV -Append
    exit 1
  }
  $m = [regex]::Match((Get-Content $marker -Raw), 'PROBE_EXIT=(\d+)')
  $probeExit = if ($m.Success) { [int]$m.Groups[1].Value } else { -1 }
  $log = "$ws\probe-results\logs\f-e2e-probe.txt"
  $txt = if (Test-Path $log) { Get-Content $log -Raw -Encoding utf8 } else { '' }
  Get-Content $log -ErrorAction SilentlyContinue | Write-Host
  if ($probeExit -eq 0 -and $txt -match 'RESULT=PASS') {
    Write-Host "f: SYSTEM/Session 0 计划任务灌音闭环 PASS（waveIn 录到 1kHz 正弦）"
    "ITEM_F=PASS" | Out-File -FilePath $env:GITHUB_ENV -Append
  } else {
    Write-Host "::error::f: e2e_probe 未 PASS（probe_exit=$probeExit）——灌音闭环失败"
    "ITEM_F=FAIL" | Out-File -FilePath $env:GITHUB_ENV -Append
    exit 1
  }
} finally {
  # 失败路径也删：避免残留任务影响同 job 后续步骤
  & schtasks /delete /tn WomicE2EF /f 2>&1 | Out-Null
}
exit 0

exit 0
}

# windows-e2e.yml / verify / g. R3 权限裁决：标准用户 2×2 矩阵（会话0 + 交互会话）

if ($Stage -eq 's14') {
$user = 'womic-e2e-probe'
$ws = $env:GITHUB_WORKSPACE
# 计划任务的 cmd 环境不继承 Actions 进程 PATH；运行时解析 Python 路径并显式传入
$py = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $py) {
  Write-Host "::error::g: 当前 runner PATH 中找不到 Python"
  exit 1
}
# 每次运行生成随机临时密码；它只用于本次 runner 上的探针账户。
$passwordBytes = New-Object byte[] 24
[System.Security.Cryptography.RandomNumberGenerator]::Fill($passwordBytes)
$pass = 'Womic-' + [Convert]::ToBase64String($passwordBytes).TrimEnd('=').Replace('+', 'A').Replace('/', 'B') + '!a1'
# 建用户：run 39904485784 真机教训——net user ... /add 在 pwsh 7
# 下返回
# 非零退出码，且错误文本被 | Out-Null 吞掉，日志只剩
# "No valid response was provided."（net.exe 的通用错误输出），
# 无法定位失败原因。改用 pwsh 原生的 New-LocalUser + try/catch，
# 让完整错误信息暴露出来；-ErrorAction Stop 把非终止错误转终止
# 错误交给 catch。$sec 前置创建：New-LocalUser 与 g2 的
# Start-Process -Credential 共用同一个 SecureString。
$sec = ConvertTo-SecureString $pass -AsPlainText -Force
# 幂等：账户已存在时同步更新随机密码，使后续任务仍可用本次凭据。
try {
  if (-not (Get-LocalUser -Name $user -ErrorAction SilentlyContinue)) {
    New-LocalUser -Name $user -Password $sec `
      -Description "WOMic E2E R3 标准用户裁决" -ErrorAction Stop | Out-Null
  } else {
    Set-LocalUser -Name $user -Password $sec -ErrorAction Stop
  }
} catch {
  Write-Host "::error::g: 创建或更新标准用户失败: $($_.Exception.Message)"
  exit 1
}
$dir = "$env:TEMP\womic-stduser"
New-Item -ItemType Directory -Force -Path $dir | Out-Null
# 标准用户对默认 TEMP 子目录未必有写权限，显式授权
$acl = Get-Acl $dir
$acl.SetAccessRule((New-Object System.Security.AccessControl.FileSystemAccessRule(
  $user, 'Modify', 'ContainerInherit,ObjectInherit', 'None', 'Allow')))
Set-Acl $dir $acl
# 预建收证目录（Copy-Item 静默失败教训：先确保目标目录存在）
New-Item -ItemType Directory -Force -Path "$ws\probe-results\logs" | Out-Null
# 标准用户进不去 workspace（各级 ACL 只允许 runner/Administrators/
# SYSTEM traverse）：由 elevated pwsh 把探针复制到已授权的 $dir，
# g1/g2 都从这里跑。复制后必须验证目标存在（Copy-Item 会静默失败）。
$probeSrc = "$ws\client\tools\e2e_probe.py"
$probeDst = "$dir\e2e_probe.py"
if (-not (Test-Path $probeSrc)) {
  Write-Host "::error::g: 探针源文件不存在: $probeSrc"
  exit 1
}
Copy-Item $probeSrc $probeDst -Force
if (-not (Test-Path $probeDst)) {
  Write-Host "::error::g: 探针副本不存在（Copy-Item 静默失败？src=$probeSrc）"
  exit 1
}

# ---- 公共小工具（本步骤作用域内复用；判据只看 RESULT= 行与 marker）----
function Write-ProbeBat($bat, $marker, $outDir, $stdoutTxt) {
  # 探针跑在 $dir（已授权标准用户 Modify）里，不依赖 workspace；
  # 所有输入输出路径均为绝对路径；goto 标签顶行首；cd 失败时写
  # PROBE_EXIT=97（与"探针 FAIL"区分，便于定位是基础设施问题）。
  $lines = @(
    '@echo off',
    'chcp 65001 >nul 2>&1',
    'set PYTHONIOENCODING=utf-8',
    "cd /d `"$dir`" || goto :fail",
    "`"$py`" e2e_probe.py --seconds 3 --out `"$outDir`" > `"$stdoutTxt`" 2>&1",
    'set RC=%ERRORLEVEL%',
    "echo PROBE_EXIT=%RC% > `"$marker`"",
    'exit /b %RC%',
    ':fail',
    "echo PROBE_EXIT=97 > `"$marker`"",
    'exit /b 97'
  )
  $lines | Set-Content -Path $bat -Encoding ascii
}
function Wait-ProbeMarker($marker, $minutes) {
  $deadline = (Get-Date).AddMinutes($minutes)
  while (-not (Test-Path $marker) -and (Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 3
  }
  return (Test-Path $marker)
}
function Get-ProbeExit($marker) {
  if (-not (Test-Path $marker)) { return -1 }
  $mm = [regex]::Match((Get-Content $marker -Raw), 'PROBE_EXIT=(\d+)')
  if ($mm.Success) { return [int]$mm.Groups[1].Value }
  return -1
}
function Get-ProbeVerdict($stdoutTxt) {
  if (-not (Test-Path $stdoutTxt)) { return 'ERROR' }
  $mm = [regex]::Match((Get-Content $stdoutTxt -Raw -Encoding utf8), 'RESULT=(PASS|FAIL)')
  if ($mm.Success) { return $mm.Groups[1].Value }
  return 'ERROR'
}
function Save-ProbeEvidence($stdoutTxt, $outDir, $logDst, $probeDst) {
  # Copy-Item -ErrorAction SilentlyContinue 会吞错：复制后必须验证目标
  if (Test-Path $stdoutTxt) {
    Copy-Item $stdoutTxt $logDst -Force
    if (-not (Test-Path $logDst)) {
      Write-Host "::warning::g: 日志复制后仍不存在: $logDst"
    }
  } else {
    Write-Host "::warning::g: 探针 stdout 不存在: $stdoutTxt"
  }
  if (Test-Path $outDir) {
    New-Item -ItemType Directory -Force -Path $probeDst | Out-Null
    Copy-Item "$outDir\*" $probeDst -Recurse -Force -ErrorAction SilentlyContinue
    if (-not (Get-ChildItem $probeDst -ErrorAction SilentlyContinue)) {
      Write-Host "::warning::g: 探针输出复制后为空: $probeDst"
    }
  } else {
    Write-Host "::warning::g: 探针输出目录不存在: $outDir"
  }
}

# ---- g1：标准用户 × 计划任务（Session 0）----
# 任务以标准用户身份跑，进程落在 Session 0（非交互）——与 f 的
# (SYSTEM, s0) 只差用户权限一个变量，权限维度由此分离。
#
# 五轮失败的演进（勿删，团队知识；后两轮由 schedtask-diag
# workflow 的变体实验定性，run 39926625705）：
# * run 39906824245：schtasks /create /tr /ru /rp（无 /xml），
#   schtasks /query 显示 Interactive/Background、Last Result
#   267011（SCHED_S_TASK_HAS_NOT_RUN），marker 4 分钟不出现。
# * run 39910125177/39910107431：Register-ScheduledTask
#   -Principal ... -Password 报 "Parameter set cannot be
#   resolved"——两参数不属同一参数集（-Password 只在 User/Xml/
#   Object 集，-Principal 只在 Principal 集）。
# * run 39921983841：schtasks /create /xml(LogonType=Password)
#   + /ru /rp，仍显示 Interactive/Background + schtasks 警告
#   "Batch logon privilege needs to be enabled"。
# * run 39924487481：Register-ScheduledTask -User -Password
#   （User 集）注册成功，schtasks /query 仍显示
#   Interactive/Background——当时误判为登录类型错误。
# * schedtask-diag run 39926625705 一次性变体实验定性：
#   1) schtasks /query 的 "Logon Mode: Interactive/Background"
#      是显示层假象——Get-ScheduledTask 的 Principal.LogonType
#      实测为 Password（cmdlet User/Xml/Object 三集皆然）。
#      判据必须用 Get-ScheduledTask，不能用 schtasks 显示。
#   2) 任务 /run 后从不启动（marker 不出现、Last Result 一直
#      停在 267011）的真正原因：SeBatchLogonRight 缺失。
#      secedit /export USER_RIGHTS 实证该权限只有内置四组
#      （Administrators/Backup Operators/Performance Log
#      Users/Performance Monitor Users），探针用户不在其中。
#      MS Learn《作为批处理作业登录》所称"任务计划程序自动
#      授予"只对 UI 向导成立，API/cmdlet 注册不自动授予。
#   3) -Password 必须传 String：传 SecureString 会被 ToString
#      成垃圾，注册即报 "The user name or password is
#      incorrect"（V2 实证）。
# 定案：secedit 导出 USER_RIGHTS → 把探针用户 SID 追加进
# SeBatchLogonRight → 导入（只加这一个权限，其余原样带回）
# → 验证在位 → Register-ScheduledTask User 集注册（-User +
# -Password String + -RunLevel Limited）→ Get-ScheduledTask
# 校验 Principal.LogonType=Password → schtasks /run 触发。
$bat1 = "$dir\run_g1.cmd"
$marker1 = "$dir\g1_exit.txt"
$out1 = "$dir\probe-g1"
$so1 = "$dir\g1-stdout.txt"
if (Test-Path $marker1) { Remove-Item $marker1 -Force }
Write-ProbeBat $bat1 $marker1 $out1 $so1
# ---- 0) 授予 SeBatchLogonRight（Password logon 的硬前提）----
# secedit /configure /areas USER_RIGHTS 会整块替换用户权限区，
# 因此必须先 /export 当前状态、只往 SeBatchLogonRight 追加一个
# SID、再原样导回——其余权限一字不动。
$sid = (Get-LocalUser -Name $user).SID.Value
$inf = "$dir\userrights.inf"
$sdb = "$dir\userrights.sdb"
& secedit /export /cfg $inf /areas USER_RIGHTS 2>&1 | Out-Null
if (-not (Test-Path $inf)) {
  Write-Host "::error::g1: secedit 导出 USER_RIGHTS 失败"
  "ITEM_G1=ERROR" | Out-File -FilePath $env:GITHUB_ENV -Append
  exit 1
}
$lines = Get-Content $inf
$patched = $false
for ($i = 0; $i -lt $lines.Count; $i++) {
  if ($lines[$i] -match '^\s*SeBatchLogonRight\s*=\s*(.*)$') {
    # 已授权判定要同时认 SID 与账户名：secedit /export 会把能解析
    # 的 SID 写成账户名（2026-10-23 真机实证：导出为
    # "womic-e2e-probe,*S-1-5-32-544,..."），只比 SID 会重复追加。
    if (($matches[1] -notmatch [regex]::Escape($sid)) -and
        ($matches[1] -notmatch [regex]::Escape($user))) {
      $lines[$i] = "SeBatchLogonRight = $($matches[1].TrimEnd()),*$sid"
    }
    $patched = $true
    break
  }
}
if (-not $patched) {
  # 极端情况：导出里没有这一行——插到 [Privilege Rights] 节头后
  for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^\[Privilege Rights\]') {
      $lines = @($lines[0..$i]) + @("SeBatchLogonRight = *$sid") + @($lines[($i+1)..($lines.Count-1)])
      $patched = $true
      break
    }
  }
}
if (-not $patched) {
  Write-Host "::error::g1: USER_RIGHTS 导出里找不到 SeBatchLogonRight 行"
  "ITEM_G1=ERROR" | Out-File -FilePath $env:GITHUB_ENV -Append
  exit 1
}
Set-Content -Path $inf -Value $lines -Encoding Unicode
& secedit /configure /db $sdb /cfg $inf /areas USER_RIGHTS 2>&1 | Out-String | Write-Host
# 授予后必须验证：再导一次，不在位就当场炸（不带着假授权往下走）。
# 2026-10-23 真机实证（debug-tailscale 交互会话）：secedit /export
# 会把能解析的 SID 写成账户名，导出行为
#   SeBatchLogonRight = womic-e2e-probe,*S-1-5-32-544,...
# 因此判据必须 SID 或账户名任一命中——只比 SID 会假失败。
$inf2 = "$dir\userrights2.inf"
& secedit /export /cfg $inf2 /areas USER_RIGHTS 2>&1 | Out-Null
$chk = Get-Content $inf2 -ErrorAction SilentlyContinue |
  Where-Object { $_ -match 'SeBatchLogonRight' }
$sidOk = $chk -and (("$chk" -match [regex]::Escape($sid)) -or ("$chk" -match [regex]::Escape($user)))
if (-not $sidOk) {
  Write-Host "::error::g1: SeBatchLogonRight 授予后验证失败（SID=$sid / user=$user 均不在位）"
  "ITEM_G1=ERROR" | Out-File -FilePath $env:GITHUB_ENV -Append
  exit 1
}
Write-Host "g1: SeBatchLogonRight 已授予 $env:COMPUTERNAME\$user（SID=$sid，已验证）"
# ---- 1) 注册：Register-ScheduledTask User（默认）参数集 ----
# 无触发器：靠 schtasks /run 强制立即触发（不依赖任何计划时间）
$action1 = New-ScheduledTaskAction -Execute 'cmd.exe' -Argument "/c `"$bat1`"" -WorkingDirectory $dir
$settings1 = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 10) -MultipleInstances IgnoreNew
try {
  Register-ScheduledTask -TaskName WomicE2EG1 -Action $action1 -Settings $settings1 `
    -User "$env:COMPUTERNAME\$user" -Password $pass -RunLevel Limited `
    -Force -ErrorAction Stop | Out-Null
} catch {
  Write-Host "::error::g1: 注册计划任务失败: $($_.Exception.Message)"
  "ITEM_G1=ERROR" | Out-File -FilePath $env:GITHUB_ENV -Append
  exit 1
}
# ---- 2) 登录类型校验：权威判据是 Get-ScheduledTask 的
#      Principal.LogonType，不是 schtasks /query 的显示
#      （schedtask-diag 实证：Password 任务被 schtasks 显示成
#      Interactive/Background，显示层不可信）----
$lt1 = (Get-ScheduledTask -TaskName WomicE2EG1 -ErrorAction Stop).Principal.LogonType
Write-Host "g1: Principal.LogonType='$lt1'（期望 Password）"
if ("$lt1" -ne 'Password') {
  Write-Host "::error::g1: Principal.LogonType 不是 Password"
  "ITEM_G1=ERROR" | Out-File -FilePath $env:GITHUB_ENV -Append
  & schtasks /delete /tn WomicE2EG1 /f 2>&1 | Out-Null
  exit 1
}
& schtasks /run /tn WomicE2EG1 2>&1 | Out-String | Write-Host
if ($LASTEXITCODE -ne 0) {
  Write-Host "::error::g1: 触发计划任务 WomicE2EG1 失败"
  "ITEM_G1=ERROR" | Out-File -FilePath $env:GITHUB_ENV -Append
  & schtasks /delete /tn WomicE2EG1 /f 2>&1 | Out-Null
  exit 1
}
# 快速失败探测：任务无法启动（密码错/凭据被拒/无批登录权限）时
# Last Result 立即变非 0。三种非失败值要放行：
#   0      = 已跑完
#   267011 = SCHED_S_TASK_HAS_NOT_RUN（从未运行，刚注册）
#   267009 = SCHED_S_TASK_IS_RUNNING（正在运行——run 39973799266
#            实证：探针每 5s 查一次，恰好撞上任务运行中，被误判
#            为启动失败；真机会话里 marker 秒出所以没暴露）
$fastFail = $false
for ($i = 0; $i -lt 12 -and -not (Test-Path $marker1); $i++) {
  Start-Sleep -Seconds 5
  $q = & schtasks /query /tn WomicE2EG1 /fo LIST /v 2>&1 | Out-String
  $lr = [regex]::Match($q, 'Last Result:\s*(\S+)')
  if ($lr.Success -and $lr.Groups[1].Value -notin @('0', '267011', '267009')) {
    Write-Host "::error::g1: 计划任务启动即失败（Last Result=$($lr.Groups[1].Value)）"
    Write-Host $q
    $fastFail = $true
    break
  }
}
if ($fastFail -or -not (Wait-ProbeMarker $marker1 4)) {
  if (-not $fastFail) {
    Write-Host "::error::g1: 计划任务探针 4 分钟超时（marker 未出现）"
    & schtasks /query /tn WomicE2EG1 /fo LIST /v 2>&1 | Out-String | Write-Host
    # 附赠诊断：Password logon 需要 SeBatchLogonRight。导出本机
    # USER_RIGHTS 看任务计划程序是否如文档所说自动授予了该权限
    # （Microsoft Learn《作为批处理作业登录》）。不修改任何设置。
    & secedit /export /cfg "$dir\userrights.inf" /areas USER_RIGHTS 2>&1 | Out-Null
    if (Test-Path "$dir\userrights.inf") {
      Get-Content "$dir\userrights.inf" |
        Where-Object { $_ -match 'SeBatchLogonRight|SeInteractiveLogonRight' } |
        Out-String | Write-Host
    }
  }
  "ITEM_G1=ERROR" | Out-File -FilePath $env:GITHUB_ENV -Append
  & schtasks /delete /tn WomicE2EG1 /f 2>&1 | Out-Null
  exit 1
}
$exit1 = Get-ProbeExit $marker1
Save-ProbeEvidence $so1 $out1 "$ws\probe-results\logs\g1-e2e-probe-stduser-s0.txt" "$ws\probe-results\probe-stduser-s0"
$v1 = Get-ProbeVerdict $so1
"ITEM_G1=$v1" | Out-File -FilePath $env:GITHUB_ENV -Append
Write-Host "::notice::g1: 标准用户×会话0 灌音结果 = $v1（probe_exit=$exit1，R3 裁决数据）"
& schtasks /delete /tn WomicE2EG1 /f 2>&1 | Out-Null

# ---- g2：标准用户 × 交互会话（原有实现，保留作对照）----
$bat2 = "$dir\run_g2.cmd"
$marker2 = "$dir\g2_exit.txt"
$out2 = "$dir\probe-g2"
$so2 = "$dir\g2-stdout.txt"
if (Test-Path $marker2) { Remove-Item $marker2 -Force }
Write-ProbeBat $bat2 $marker2 $out2 $so2
# $sec 在建用户处已创建（New-LocalUser 与 -Credential 共用），此处直接用
$cred = New-Object System.Management.Automation.PSCredential("$env:COMPUTERNAME\$user", $sec)
Start-Process -FilePath 'cmd.exe' -ArgumentList "/c `"$bat2`"" `
  -Credential $cred -WorkingDirectory $dir -Wait
$exit2 = Get-ProbeExit $marker2
Save-ProbeEvidence $so2 $out2 "$ws\probe-results\logs\g2-e2e-probe-stduser-interactive.txt" "$ws\probe-results\probe-stduser-interactive"
$v2raw = Get-ProbeVerdict $so2
# 三态判定（团队约定，见 docs/ENVIRONMENT.md §5）：
#   PASS / UNSUPPORTED / FAIL
# 交互会话下"测过、结果明确=不可采集"记 UNSUPPORTED——带原因与原始
# rc，不计产品缺陷；只有基础设施/脚本错误才 ERROR。原始 RESULT 全量落盘。
$v2 = if ($v2raw -eq 'FAIL') { 'UNSUPPORTED' } else { $v2raw }
"ITEM_G2=$v2" | Out-File -FilePath $env:GITHUB_ENV -Append
Write-Host "::notice::g2: 标准用户×交互会话 = $v2（原始 RESULT=$v2raw，probe_exit=$exit2；UNSUPPORTED=该上下文不支持采集，非缺陷）"

# ---- 裁决汇总：2×2 矩阵 + 历史记录 ----
$fRes = if ($env:ITEM_F) { $env:ITEM_F } else { 'n/a' }
Write-Host "g: 2×2 裁决矩阵 {(SYSTEM,s0)=$fRes, (std,s0)=$v1, (std,interactive)=$v2}；历史 (elevated,interactive)=UNSUPPORTED（run 39901322823，原始 FAIL）"
Write-Host "::notice::g: 若 g1/g2 分化而 f=PASS，结论=灌音约束取决于会话上下文而非用户权限"
$rows = @(
  '### R3 权限裁决矩阵（三态判定）',
  '',
  '| 上下文 | 探针结果 | 判定 | 说明 |',
  '|---|---|---|---|',
  "| (SYSTEM, session 0) | $fRes | $fRes | 服务/计划任务路径，代表产品灌音能力（须 PASS） |",
  "| (标准用户, session 0) | $v1 | $v1 | 身份维度对照 |",
  "| (标准用户, 交互会话) | $v2 | $v2 | UNSUPPORTED = 该上下文不支持采集（会话隔离，rc 见日志），非缺陷 |",
  '| (管理员, 交互会话) | UNSUPPORTED | UNSUPPORTED | 历史 run 39901322823；与权限无关 |'
)
$rows | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
# g 是裁决记录步骤：探针 FAIL 不失败作业（结论写报告），显式 exit 0
exit 0

exit 0
}

# windows-e2e.yml / verify / h. GUI 冒烟：无参启动 6 秒进程存活

if ($Stage -eq 's16') {
$p = Start-Process -FilePath "$env:GITHUB_WORKSPACE\dist\womic.exe" `
  -WorkingDirectory "$env:GITHUB_WORKSPACE\dist" -PassThru
Start-Sleep -Seconds 6
$proc = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
$alive = $null -ne $proc
$title = if ($proc) { $proc.MainWindowTitle } else { '' }
"alive=$alive title='$title'" | Tee-Object probe-results/logs/h-gui-smoke.txt
if (-not $alive) {
  Write-Host "::error::h: womic.exe 启动 6 秒后进程退出（GUI 冒烟失败）"
  "ITEM_H=FAIL" | Out-File -FilePath $env:GITHUB_ENV -Append
  exit 1
}
Stop-Process -Id $p.Id -Force
Write-Host "h: GUI 存活 6s（主窗口标题='$title'）"
"ITEM_H=PASS" | Out-File -FilePath $env:GITHUB_ENV -Append
exit 0

exit 0
}

# windows-e2e.yml / verify / h2. GUI 界面截图（标题断言 + 窗口截图）

if ($Stage -eq 's17') {
$dir = "probe-results/screenshots"
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$env:MICLINK_EXPAND_ALL = '1'
$env:MICLINK_SHOW_MONITOR = '1'
$env:MICLINK_SHOW_HELP = '1'
$p = Start-Process -FilePath "$env:GITHUB_WORKSPACE\dist\womic.exe" `
  -WorkingDirectory "$env:GITHUB_WORKSPACE\dist" -PassThru
Remove-Item Env:MICLINK_EXPAND_ALL, Env:MICLINK_SHOW_MONITOR, Env:MICLINK_SHOW_HELP

# 等主窗口就绪（最多 10s，每 0.5s 轮询一次）
$hwnd = [IntPtr]::Zero
for ($i = 0; $i -lt 20; $i++) {
  Start-Sleep -Milliseconds 500
  $p.Refresh()
  if ($p.MainWindowHandle -ne [IntPtr]::Zero) { $hwnd = $p.MainWindowHandle; break }
}
$title = if ($p.MainWindowTitle) { $p.MainWindowTitle } else { '' }
"hwnd=$($hwnd.ToInt64()) title='$title'" | Tee-Object "$dir/h2-gui-info.txt"

if ($hwnd -eq [IntPtr]::Zero) {
  Write-Host "::error::h2: 10 秒内未拿到主窗口句柄（GUI 未起）"
  "ITEM_H2=FAIL" | Out-File -FilePath $env:GITHUB_ENV -Append
  Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
  exit 1
}
if ($title -notlike '*MicLink*') {
  Write-Host "::error::h2: 主窗口标题不含 'MicLink'（实得 '$title'）"
  "ITEM_H2=FAIL" | Out-File -FilePath $env:GITHUB_ENV -Append
  Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
  exit 1
}

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W32 {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
"@
[W32]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 800
$r = New-Object W32+RECT
[W32]::GetWindowRect($hwnd, [ref]$r) | Out-Null
$w = $r.R - $r.L
$h = $r.B - $r.T
"rect=${w}x${h}" | Out-File -Append "$dir/h2-gui-info.txt"
$shotOk = $false
if ($w -gt 0 -and $h -gt 0) {
  $bmp = New-Object System.Drawing.Bitmap($w, $h)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  $ok = [W32]::PrintWindow($hwnd, $hdc, 2)
  $g.ReleaseHdc($hdc)
  $g.Dispose()
  $bmp.Save("$dir/gui-main.png", [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
  "printwindow=$ok" | Out-File -Append "$dir/h2-gui-info.txt"
  $shotOk = Test-Path "$dir/gui-main.png"
}
# Monitor and help screenshots are captured during the native UI test, when
# those child windows are known to be visible in the interactive session.
# 整屏兜底：PrintWindow 在个别会话下会出全黑，留一张屏幕截图对照
try {
  Add-Type -AssemblyName System.Windows.Forms
  $vs = [System.Windows.Forms.SystemInformation]::VirtualScreen
  $bmp2 = New-Object System.Drawing.Bitmap($vs.Width, $vs.Height)
  $g2 = [System.Drawing.Graphics]::FromImage($bmp2)
  $g2.CopyFromScreen($vs.X, $vs.Y, 0, 0, $bmp2.Size)
  $g2.Dispose()
  $bmp2.Save("$dir/screen-full.png", [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp2.Dispose()
  $shotOk = $true
} catch {
  Write-Host "::warning::h2: 整屏兜底截图失败：$($_.Exception.Message)"
}
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
if ($shotOk) {
  Write-Host "h2: 截图完成（$dir）"
  "ITEM_H2=PASS" | Out-File -FilePath $env:GITHUB_ENV -Append
  exit 0
}
Write-Host "::warning::h2: 截图未产出（窗口存在但抓图失败，可能无桌面会话）"
"ITEM_H2=WARN" | Out-File -FilePath $env:GITHUB_ENV -Append
exit 0

exit 0
}

# windows-e2e.yml / verify / i. 汇总验证结论

if ($Stage -eq 's18') {
# a 的结论从其日志的最后一行读（exit=/ok_lines=），其余项来自各步骤
# 写入 GITHUB_ENV 的 ITEM_* 变量；没跑到的步骤显示为 n/a。
$itemA = 'n/a'
if (Test-Path probe-results/logs/a-test_drvpkg.txt) {
  $last = Get-Content probe-results/logs/a-test_drvpkg.txt | Select-Object -Last 1
  if ($last -match 'ok_lines=9') { $itemA = 'PASS' } else { $itemA = "FAIL ($last)" }
}
$rows = [ordered]@{
  'a 驱动包解压自检（9/9）' = $itemA
  'b CLI 帮助文本完整'      = $(if ($env:ITEM_B) { $env:ITEM_B } else { 'n/a' })
  'c 未装驱动 status=3'     = $(if ($env:ITEM_C) { $env:ITEM_C } else { 'n/a' })
  'd 驱动安装系统状态'      = $(if ($env:INSTALL_STATE) { $env:INSTALL_STATE } else { 'n/a' })
  'e 已装驱动 status=0'     = $(if ($env:ITEM_E) { $env:ITEM_E } else { 'n/a' })
  'f 端到端灌音（核心）'     = $(if ($env:ITEM_F) { $env:ITEM_F } else { 'n/a' })
  'g1 标准用户×会话0 灌音'   = $(if ($env:ITEM_G1) { $env:ITEM_G1 } else { 'n/a' })
  'g2 标准用户×交互会话 灌音' = $(if ($env:ITEM_G2) { $env:ITEM_G2 } else { 'n/a' })
  'h GUI 冒烟（6s 存活）'   = $(if ($env:ITEM_H) { $env:ITEM_H } else { 'n/a' })
  'h2 GUI 界面截图（标题+画面）' = $(if ($env:ITEM_H2) { $env:ITEM_H2 } else { 'n/a' })
}
$md = @()
$md += '## Windows E2E 验证结论'
$md += ''
$md += '| 验证项 | 结果 |'
$md += '|---|---|'
foreach ($k in $rows.Keys) { $md += "| $k | $($rows[$k]) |" }
$md += ''
$md += "客户端自助安装退出码: $env:CLIENT_INSTALL_EXIT（安装器原始回执见报告；另以系统状态与 status 门禁复核）"
# R3 2×2 裁决矩阵：(SYSTEM,s0)=f / (std,s0)=g1 / (std,interactive)=g2；
# 历史 (elevated,interactive)=FAIL（run 39901322823）。矩阵齐备时
# 结论应为"灌音约束取决于会话上下文而非用户权限"。
$g1s = $(if ($env:ITEM_G1) { $env:ITEM_G1 } else { 'n/a' })
$g2s = $(if ($env:ITEM_G2) { $env:ITEM_G2 } else { 'n/a' })
$fs = $(if ($env:ITEM_F) { $env:ITEM_F } else { 'n/a' })
$md += "R3 2×2 裁决矩阵: (SYSTEM,s0)=$fs ; (std,s0)=$g1s ; (std,interactive)=$g2s ; 历史(elevated,interactive)=UNSUPPORTED（run 39901322823，原始 FAIL）"
$md | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
# 同步落一份到 probe-results 根：既保证 upload-artifact 的
# 公共祖先目录是 probe-results（否则只有 logs/ 有文件时 artifact
# 会把 logs/ 拍平到根，Round 1 实测如此），也便于离线诊断。
$md | Out-File -FilePath probe-results/verdict.txt -Encoding utf8

exit 0
}
throw 'unknown stage'
