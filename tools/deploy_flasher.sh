#!/usr/bin/env bash
# Manual flasher deploy to apps.k7swi.org/LoRa_Tracker/
#
# Usage:
#   tools/deploy_flasher.sh --tag v1.2.3 [--key ~/.ssh/deploy_key]
#
# Requires:
#   - rsync, ssh, python3
#   - SSH access to DEPLOY_USER@DEPLOY_HOST with write permission to DEPLOY_PATH
#
# Environment variables (or edit the defaults below):
#   DEPLOY_HOST   — server hostname         (default: apps.k7swi.org)
#   DEPLOY_USER   — SSH login user          (default: current $USER)
#   DEPLOY_PATH   — server-side directory   (default: /var/www/html/LoRa_Tracker)
#   DEPLOY_KEY    — path to SSH private key (default: ~/.ssh/id_ed25519)
#   GITHUB_REPO   — owner/repo              (default: KJ7NYE/LoRa_FieldOps_APRS_Tracker)

set -euo pipefail

DEPLOY_HOST="${DEPLOY_HOST:-apps.k7swi.org}"
DEPLOY_USER="${DEPLOY_USER:-${USER}}"
DEPLOY_PATH="${DEPLOY_PATH:-/var/www/html/LoRa_Tracker}"
DEPLOY_KEY="${DEPLOY_KEY:-${HOME}/.ssh/id_ed25519}"
GITHUB_REPO="${GITHUB_REPO:-KJ7NYE/LoRa_FieldOps_APRS_Tracker}"

TAG=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --tag)  TAG="$2"; shift 2 ;;
    --key)  DEPLOY_KEY="$2"; shift 2 ;;
    *)      echo "Unknown option: $1"; exit 1 ;;
  esac
done

if [[ -z "$TAG" ]]; then
  echo "Error: --tag is required (e.g. --tag v1.2.3)" >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "==> Generating manifests for ${TAG}..."
python3 "${SCRIPT_DIR}/gen_manifests.py" \
  --tag "${TAG}" \
  --repo "${GITHUB_REPO}" \
  --output "${REPO_ROOT}/flasher/manifests"

echo "==> Deploying flasher/ to ${DEPLOY_USER}@${DEPLOY_HOST}:${DEPLOY_PATH}/ ..."
rsync -az --delete \
  -e "ssh -i ${DEPLOY_KEY} -o StrictHostKeyChecking=yes" \
  "${REPO_ROOT}/flasher/" \
  "${DEPLOY_USER}@${DEPLOY_HOST}:${DEPLOY_PATH}/"

echo "==> Done. Flasher live at https://${DEPLOY_HOST}/LoRa_Tracker/"
