#!/bin/bash
#
# LinuxBox image build on top of an unmodified upstream Armbian checkout.
#
#   ./build.sh [--armbian <tag>] [--release <codename>] [--board <board>] [-- <extra compile.sh args>]
#
# The Armbian tree lives in armbian/<tag>/ and is never edited; userpatches/
# is synced into it before each build.  Each Armbian version keeps its own
# cache/ and output/ inside armbian/<tag>/ (images: armbian/<tag>/output/images).

set -euo pipefail

HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
source "${HERE}/versions.conf"

ARMBIAN_TAG="${ARMBIAN_DEFAULT_TAG}"
RELEASE="bookworm"
BOARD="linuxbox-hubv3"
EXTRA_ARGS=()

usage() {
	sed -n '3,9p' "$0" | sed 's/^# \{0,1\}//'
	echo
	echo "Supported Armbian versions and releases:"
	for tag in "${!ARMBIAN_RELEASES[@]}"; do
		echo "  ${tag}: ${ARMBIAN_RELEASES[$tag]}"
	done | sort -V
	exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--armbian) ARMBIAN_TAG="$2"; shift 2 ;;
		--release) RELEASE="$2"; shift 2 ;;
		--board) BOARD="$2"; shift 2 ;;
		-h | --help) usage 0 ;;
		--) shift; EXTRA_ARGS=("$@"); break ;;
		*) echo "Unknown option: $1" >&2; usage 1 ;;
	esac
done

supported="${ARMBIAN_RELEASES[$ARMBIAN_TAG]:-}"
[[ -n "${supported}" ]] || { echo "Armbian '${ARMBIAN_TAG}' is not in versions.conf" >&2; usage 1; }
[[ " ${supported} " == *" ${RELEASE} "* ]] || {
	echo "Release '${RELEASE}' is not supported by Armbian ${ARMBIAN_TAG} (supported: ${supported})" >&2
	exit 1
}
[[ -f "${HERE}/userpatches/config/boards/${BOARD}.conf" ]] || { echo "Unknown board '${BOARD}'" >&2; exit 1; }

armbian_dir="${HERE}/armbian/${ARMBIAN_TAG}"
if [[ ! -d "${armbian_dir}/.git" ]]; then
	git clone --depth=1 --branch "${ARMBIAN_TAG}" "${ARMBIAN_GIT_URL}" "${armbian_dir}"
fi
if [[ -n "$(git -C "${armbian_dir}" status --porcelain --untracked-files=no)" ]]; then
	echo "armbian/${ARMBIAN_TAG} has local modifications; Armbian must stay pristine:" >&2
	git -C "${armbian_dir}" status --short --untracked-files=no >&2
	exit 1
fi

rsync -a --delete "${HERE}/userpatches/" "${armbian_dir}/userpatches/"

cd "${armbian_dir}"
# Armbian decides where to run: root builds natively, a non-root docker-group user
# is relaunched in Armbian's container.  'compile.sh docker' must not be used as
# root: the container then re-runs 'docker' and aborts.
run_args=()
[[ "${EUID}" == "0" ]] && run_args+=(ALLOW_ROOT=yes)
# Armbian's patching.py does int($COLUMNS); an empty value (nohup/CI) crashes it.
[[ -n "${COLUMNS:-}" ]] || export COLUMNS=160
# The prebuilt ghcr.io image (~850MB) downloads at ~80KB/s here, so the container is
# built from the plain base image Armbian picks (pulled via the Docker Hub mirror).
exec ./compile.sh build "${run_args[@]}" \
	DOCKERFILE_USE_ARMBIAN_IMAGE_AS_BASE="${DOCKERFILE_USE_ARMBIAN_IMAGE_AS_BASE:-no}" \
	BOARD="${BOARD}" BRANCH=current RELEASE="${RELEASE}" \
	BUILD_MINIMAL=no BUILD_DESKTOP=no KERNEL_CONFIGURE=no \
	EXPERT=yes KERNEL_GIT=shallow \
	NETWORKING_STACK="network-manager" EXTRAWIFI=no \
	VENDOR="armbian" MAINTAINER="ThirdReality" MAINTAINERMAIL="support@3reality.com" \
	"${EXTRA_ARGS[@]}"
