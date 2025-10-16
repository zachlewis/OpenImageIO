#!/usr/bin/env bash

# Utility script to download or build ccache
#
# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO

# This script will either download or build ccache, and dump
# the resulting binary into $CCACHE_INSTALL_DIR/bin/ccache

# Exit the whole script if any command fails.
set -ex

echo "Building ccache"
uname
ARCH="$(uname -m)"
echo "HOME=$HOME"
echo "PWD=$PWD"
echo "ARCH=$ARCH"

# Set CCACHE_PREBUILT to 0 to build from source.
CCACHE_PREBUILT=${CCACHE_PREBUILT:=1}

# Repo and branch/tag/commit of ccache to download if we don't have it yet
CCACHE_REPO=${CCACHE_REPO:=https://github.com/ccache/ccache}
CCACHE_VERSION=${CCACHE_VERSION:=4.12.1}
CCACHE_TAG=${CCACHE_TAG:=v${CCACHE_VERSION}}
LOCAL_DEPS_DIR=${LOCAL_DEPS_DIR:=${PWD}/ext}
# Where to put ccache repo source (default to the ext area)
CCACHE_SRC_DIR=${CCACHE_SRC_DIR:=${LOCAL_DEPS_DIR}/ccache}
# Temp build area (default to a build/ subdir under source)
CCACHE_BUILD_DIR=${CCACHE_BUILD_DIR:=${CCACHE_SRC_DIR}/build}
# Install area for ccache (default to ext/dist)
CCACHE_INSTALL_DIR=${CCACHE_INSTALL_DIR:=${PWD}/ext/dist}
CCACHE_CONFIG_OPTS=${CCACHE_CONFIG_OPTS:=}


# Sets: CCACHE_DESCRIPTOR, CCACHE_ARCHIVE_EXT, CCACHE_EXEC_NAME
get_ccache_release_info () {
    local os="$(uname -s)"
    local arch="$(uname -m)"
    local ver="${CCACHE_VERSION}"

    # Determine the ccache executable name and release artifact descriptor for the current platform
    case "${os}" in
        Linux)
            case "${arch}" in
                x86_64|amd64) CCACHE_DESCRIPTOR="ccache-${ver}-linux-x86_64"; CCACHE_ARCHIVE_EXT="tar.xz" ;;
                aarch64|arm64) CCACHE_DESCRIPTOR="ccache-${ver}-linux-aarch64"; CCACHE_ARCHIVE_EXT="tar.xz" ;;
                *) echo "Unsupported Linux arch for prebuilt: ${arch}"; return 1 ;;
            esac
            CCACHE_EXEC_NAME="ccache"
            ;;
        Darwin)
            CCACHE_DESCRIPTOR="ccache-${ver}-darwin"
            CCACHE_ARCHIVE_EXT="tar.gz"
            CCACHE_EXEC_NAME="ccache"
            ;;
        MINGW*|MSYS*|CYGWIN*)
            case "${arch}" in
                aarch64|arm64) CCACHE_DESCRIPTOR="ccache-${ver}-windows-aarch64"; CCACHE_ARCHIVE_EXT="zip" ;;
                i686|i386|x86) CCACHE_DESCRIPTOR="ccache-${ver}-windows-i686"; CCACHE_ARCHIVE_EXT="zip" ;;
                *) echo "Unsupported Windows arch for prebuilt: ${arch}"; return 1 ;;
            esac
            CCACHE_EXEC_NAME="ccache.exe"
            ;;
        *)
            echo "Unsupported OS for prebuilt: ${os}"
            return 1
            ;;
    esac
}

download_and_install_ccache_prebuilt() {
    # Ddownload, extract, and dump the platform-specific prebuilt ccache binary to $CCACHE_INSTALL_DIR/bin

    get_ccache_release_info
    mkdir -p "${CCACHE_SRC_DIR}"
    pushd "${CCACHE_SRC_DIR}"

    local url="${CCACHE_REPO}/releases/download/${CCACHE_TAG}/${CCACHE_DESCRIPTOR}.${CCACHE_ARCHIVE_EXT}"
    echo "Fetching ${url}"
    rm -f "ccache.${CCACHE_ARCHIVE_EXT}"
    curl --fail --location "${url}" -o "ccache.${CCACHE_ARCHIVE_EXT}"

    rm -rf "${CCACHE_DESCRIPTOR}"
    case "${CCACHE_ARCHIVE_EXT}" in
        tar.xz) tar -xJf "ccache.tar.xz" ;;
        tar.gz) tar -xzf "ccache.tar.gz" ;;
        zip)
            if command -v unzip >/dev/null 2>&1; then
                unzip -o "ccache.zip"
            elif command -v powershell >/dev/null 2>&1; then
                powershell -NoProfile -Command "Expand-Archive -Path 'ccache.zip' -DestinationPath '.' -Force"
            else
                echo "No unzip tool available to extract zip on Windows"; exit 2
            fi
            ;;
        *) echo "Unknown archive type: ${CCACHE_ARCHIVE_EXT}"; exit 2 ;;
    esac

    mkdir -p "${CCACHE_INSTALL_DIR}/bin"
    cp -f "${CCACHE_SRC_DIR}/${CCACHE_DESCRIPTOR}/${CCACHE_EXEC_NAME}" "${CCACHE_INSTALL_DIR}/bin/${CCACHE_EXEC_NAME}"
    chmod +x "${CCACHE_INSTALL_DIR}/bin/${CCACHE_EXEC_NAME}" || true

    popd
}

build_ccache_from_source() {
    # Build ccache from source and install binary to $CCACHE_INSTALL_DIR/bin
    mkdir -p "${CCACHE_SRC_DIR}"
    if [[ ! -e "${CCACHE_SRC_DIR}/.git" ]]; then
        echo "git clone ${CCACHE_REPO} ${CCACHE_SRC_DIR}"
        git clone "${CCACHE_REPO}" "${CCACHE_SRC_DIR}"
    fi

    pushd "${CCACHE_SRC_DIR}"
    git fetch --tags || true
    echo "git checkout ${CCACHE_TAG} --force"
    git checkout "${CCACHE_TAG}" --force

    if [[ -z "${CCACHE_BUILD_DRYRUN:-}" ]]; then
        rm -rf "${CCACHE_BUILD_DIR}"
        time cmake -S . -B "${CCACHE_BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release \
                   -DCMAKE_INSTALL_PREFIX="${CCACHE_INSTALL_DIR}" \
                   -DENABLE_TESTING=OFF -DENABLE_DOCUMENTATION=OFF \
                   ${CCACHE_CONFIG_OPTS}
        time cmake --build "${CCACHE_BUILD_DIR}" --config Release --target install
    fi
    popd
}


# At last... decide whether to download or build
if [[ "${CCACHE_PREBUILT}" != "0" ]]; then
    download_and_install_ccache_prebuilt
else
    build_ccache_from_source
fi

# Post-install diagnostics
ls -la "${CCACHE_INSTALL_DIR}" || true
ls -la "${CCACHE_INSTALL_DIR}/bin" || true
echo "CCACHE_INSTALL_DIR=${CCACHE_INSTALL_DIR}"
echo "CCACHE_DIR=${CCACHE_DIR}"

# Ensure a default ccache dir exists
CCACHE_DIR="${CCACHE_DIR:-$HOME/.ccache}"
mkdir -p "${CCACHE_DIR}"
ls -la "${CCACHE_DIR}" || true

# Add ccache to the PATH for use by subsequent build steps
export PATH="${CCACHE_INSTALL_DIR}/bin:$PATH"
if [[ -x "${CCACHE_INSTALL_DIR}/bin/ccache" ]]; then
    "${CCACHE_INSTALL_DIR}/bin/ccache" -sv
elif [[ -x "${CCACHE_INSTALL_DIR}/bin/ccache.exe" ]]; then
    "${CCACHE_INSTALL_DIR}/bin/ccache.exe" -s -v
fi