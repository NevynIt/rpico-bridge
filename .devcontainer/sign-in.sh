#!/usr/bin/env bash
set -euo pipefail

if [[ ! -t 0 || ! -t 1 ]]; then
    printf 'Run this script in an interactive VS Code container terminal.\n' >&2
    exit 1
fi
if [[ "$CODEX_HOME" != /home/pico/.codex ]] || ! mountpoint -q "$CODEX_HOME"; then
    printf 'Open the pico-dev Dev Container with its Codex credential volume first.\n' >&2
    exit 1
fi

codex_auth=(codex -c 'cli_auth_credentials_store="file"')
if "${codex_auth[@]}" login status; then
    printf 'Codex is already signed in.\n'
else
    "${codex_auth[@]}" login --device-auth
    "${codex_auth[@]}" login status
fi

if [[ "$GH_CONFIG_DIR" == /home/pico/.config/gh ]] && mountpoint -q "$GH_CONFIG_DIR"; then
    printf '\nGitHub CLI sign-in is optional for interactive use. Unattended pushes use SSH.\n'
    read -r -p "Sign in to GitHub CLI now? [y/N] " answer
    if [[ "$answer" == [yY] || "$answer" == [yY][eE][sS] ]]; then
        gh auth login --hostname github.com --git-protocol https --web
    fi
fi
