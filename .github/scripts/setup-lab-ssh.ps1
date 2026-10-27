# 普通 OpenSSH，与 Tailscale 入网分离：已入网的双机调试不能再次 tailscale up。
# 管理员必须使用 administrators_authorized_keys，并修 ACL；历史公钥名保留兼容。
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($env:DEBUG_PUBKEY)) { throw 'UPTERM_DEBUG_PUBKEY is empty' }
if ($env:DEBUG_PUBKEY -notmatch '^ssh-ed25519 [A-Za-z0-9+/]+=*(?:\s|$)') { throw 'Expected an Ed25519 public key' }
if (-not (Get-Service sshd -ErrorAction SilentlyContinue)) {
    Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0 | Out-Null
}
Set-Service sshd -StartupType Manual
Start-Service sshd
$f = Join-Path $env:ProgramData 'ssh/administrators_authorized_keys'
$env:DEBUG_PUBKEY | Set-Content -Path $f -Encoding ascii
icacls $f /inheritance:r /grant '*S-1-5-32-544:F' /grant '*S-1-5-18:F' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'authorized_keys ACL failed' }
$rule = 'miclink-lab-ssh'
Get-NetFirewallRule -Name $rule -ErrorAction SilentlyContinue | Remove-NetFirewallRule
$adapter = Get-NetAdapter | Where-Object { $_.InterfaceDescription -like '*Tailscale*' } | Select-Object -First 1
if (-not $adapter) { throw 'No Tailscale adapter; refusing broad firewall fallback' }
New-NetFirewallRule -Name $rule -DisplayName $rule -InterfaceAlias $adapter.Name -Direction Inbound -Protocol TCP -LocalPort 22 -Action Allow | Out-Null
if (-not (Get-NetTCPConnection -LocalPort 22 -State Listen -ErrorAction SilentlyContinue)) { throw 'sshd is not listening' }
Write-Host 'SSH ready (Tailscale interface only)'
