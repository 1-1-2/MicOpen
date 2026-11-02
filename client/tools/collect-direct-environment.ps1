# Windows PowerShell 5.1 compatible. Read-only system queries; local output only.
[CmdletBinding()]
param([string]$OutputDirectory, [switch]$Reveal)
$ErrorActionPreference = 'Stop'
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $PSScriptRoot ('Direct-environment-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$folder = New-Item -ItemType Directory -Force -Path $OutputDirectory
$report = [ordered]@{
    schema = 1
    purpose = 'MicLink Wi-Fi Direct environment inventory'
    captured_at = (Get-Date).ToString('o')
    pairing_tested = $false
    experiment = 'PBC_ONLY'
    errors = @()
}
try {
    $os = Get-CimInstance Win32_OperatingSystem -OperationTimeoutSec 15
    $report['windows'] = $os | Select-Object Caption, Version, BuildNumber, OSArchitecture
    $build = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    $report['windows_update'] = $build | Select-Object DisplayVersion, CurrentBuildNumber, UBR
} catch { $report.errors += ('windows: ' + $_.Exception.Message) }
try {
    $report['adapters'] = @(Get-NetAdapter -IncludeHidden | Select-Object InterfaceDescription,
        InterfaceType, NdisPhysicalMedium, Status, DriverInformation, DriverFileName, DriverVersion, DriverDate)
} catch { $report.errors += ('adapters: ' + $_.Exception.Message) }
try {
    $report['network_drivers'] = @(Get-CimInstance Win32_PnPSignedDriver -Filter "DeviceClass='NET'" -OperationTimeoutSec 15 |
        Select-Object DeviceName, Manufacturer, DriverProviderName, DriverVersion, DriverDate, IsSigned)
} catch { $report.errors += ('drivers: ' + $_.Exception.Message) }
try {
    $report['wlan_service'] = Get-Service WlanSvc | Select-Object Name, Status, StartType
} catch { $report.errors += ('WlanSvc: ' + $_.Exception.Message) }
try {
    # Capture a bounded native driver query. Do not enumerate saved WLAN profiles.
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = Join-Path $env:SystemRoot 'System32\netsh.exe'
    $info.Arguments = 'wlan show drivers'
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $info
    try {
        [void]$process.Start()
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit(20000)) {
            $process.Kill()
            $process.WaitForExit()
            throw 'netsh wlan show drivers exceeded 20 seconds'
        }
        $report['wlan_driver_query'] = [ordered]@{
            exit_code = $process.ExitCode
            stdout = $stdout.GetAwaiter().GetResult()
            stderr = $stderr.GetAwaiter().GetResult()
        }
    } finally { $process.Dispose() }
} catch { $report.errors += ('wlan_driver_query: ' + $_.Exception.Message) }
$json = Join-Path $folder.FullName 'environment.json'
$report | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 -LiteralPath $json
$text = @(
    'MicLink Wi-Fi Direct: environment inventory only. No pairing test was performed.'
    ('Captured: ' + $report.captured_at)
    ''
    ($report.windows | Format-List | Out-String)
    ($report.windows_update | Format-List | Out-String)
    ($report.adapters | Format-List | Out-String)
    ($report.network_drivers | Format-List | Out-String)
    ($report.wlan_service | Format-List | Out-String)
    ([string]$report.wlan_driver_query.stdout)
    ('Query errors: ' + ($report.errors -join '; '))
)
$text | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $folder.FullName 'environment.txt')
$zip = $folder.FullName + '.zip'
Compress-Archive -LiteralPath $json, (Join-Path $folder.FullName 'environment.txt') -DestinationPath $zip -Force
Write-Host ('Created: ' + $zip)
if ($Reveal) { Start-Process explorer.exe -ArgumentList ('/select,"' + $zip + '"') }
