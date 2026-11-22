#!/usr/bin/env bash
set -euo pipefail

workspace=$(cd -- "$(dirname -- "$0")/.." && pwd -P)
: "${PICO_DEV_SSH_AUTH_SOCK:?Set this to the dedicated deploy-key agent socket}"
known_hosts="$HOME/.ssh/pico-dev-known-hosts"
if [[ ! -S "$PICO_DEV_SSH_AUTH_SOCK" || ! -r "$known_hosts" ]]; then
    printf 'A dedicated SSH agent socket and %s are required.\n' "$known_hosts" >&2
    exit 1
fi
identities=$(SSH_AUTH_SOCK="$PICO_DEV_SSH_AUTH_SOCK" ssh-add -l)
if [[ $(printf '%s\n' "$identities" | wc -l) -ne 1 ]]; then
    printf 'Load only the repository deploy key into the dedicated agent.\n' >&2
    exit 1
fi

name=$(git -C "$workspace" config --get user.name)
email=$(git -C "$workspace" config --get user.email)
origin=$(git -C "$workspace" remote get-url --push origin)
case "$origin" in
    git@github.com:*|ssh://git@github.com/*) ;;
    *) printf 'Set the origin push URL to the GitHub SSH URL for this repository.\n' >&2; exit 1 ;;
esac
permissions='permissions.pico-unattended={extends=":workspace",filesystem={"/home/pico/.codex/auth.json"="deny","/home/pico/.config/gh"="deny",":workspace_roots"={".git"="write",".devcontainer"="read"}},network={enabled=true}}'

exec docker run --rm -it --init \
    --cap-drop=ALL \
    --security-opt=no-new-privileges=true \
    --security-opt=seccomp=unconfined \
    --mount "type=bind,src=$workspace,dst=/workspace" \
    --mount "type=volume,src=pico-dev-codex,dst=/home/pico/.codex" \
    --mount "type=bind,src=$PICO_DEV_SSH_AUTH_SOCK,dst=/run/pico-ssh-agent,readonly" \
    --mount "type=bind,src=$known_hosts,dst=/run/pico-known-hosts,readonly" \
    --env SSH_AUTH_SOCK=/run/pico-ssh-agent \
    --env 'GIT_SSH_COMMAND=ssh -o BatchMode=yes -o StrictHostKeyChecking=yes -o UserKnownHostsFile=/run/pico-known-hosts' \
    --env GIT_TERMINAL_PROMPT=0 \
    --env "GIT_AUTHOR_NAME=$name" --env "GIT_COMMITTER_NAME=$name" \
    --env "GIT_AUTHOR_EMAIL=$email" --env "GIT_COMMITTER_EMAIL=$email" \
    pico-dev:latest codex \
    --ask-for-approval never \
    -c 'cli_auth_credentials_store="file"' \
    -c 'web_search="live"' \
    -c 'default_permissions="pico-unattended"' \
    -c "$permissions" \
    "$@"
