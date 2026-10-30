#!/usr/bin/env bash
set -euo pipefail

if [[ "${GITHUB_ACTIONS:-}" != "true" ]]; then
  echo "ci-cleanup.sh: not running in GitHub Actions, skipping" >&2
  exit 0
fi

sudo rm -rf \
  /usr/share/dotnet \
  /usr/local/lib/android \
  /opt/ghc \
  /opt/hostedtoolcache/CodeQL \
  /usr/local/share/boost \
  /usr/local/lib/node_modules

sudo apt-get clean
docker image prune -af || true

df -h /
