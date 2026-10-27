#!/usr/bin/env bash
# SSH 只装在显式请求的 Lab。私钥不离开操作端；公钥沿用原 Variable 名。
set -euo pipefail
[[ "${DEBUG_PUBKEY:-}" =~ ^ssh-ed25519\ [A-Za-z0-9+/]+=*([[:space:]]|$) ]] || { echo 'Expected Ed25519 UPTERM_DEBUG_PUBKEY' >&2; exit 1; }
if ! command -v sshd >/dev/null; then
  timeout -k 10 120 sudo apt-get update -qq
  timeout -k 10 180 sudo apt-get install -y -qq openssh-server
fi
mkdir -p "$HOME/.ssh"
chmod 700 "$HOME/.ssh"
printf '%s\n' "$DEBUG_PUBKEY" >> "$HOME/.ssh/authorized_keys"
chmod 600 "$HOME/.ssh/authorized_keys"
sudo systemctl start ssh || sudo service ssh start
ss -tln | grep -qE ':22[[:space:]]' || { echo 'sshd not listening' >&2; exit 1; }
echo 'SSH ready'
