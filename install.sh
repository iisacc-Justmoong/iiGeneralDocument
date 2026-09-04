#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
PREFIX="${IIGENERALDOCUMENT_INSTALL_PREFIX:-${HOME}/.local/SDK/iiGeneralDocument}"
SDK_ROOT="${IIGENERALDOCUMENT_LOCAL_LIBRARY_ROOT:-${HOME}/.local/SDK}"
QT_PREFIX="${IIGENERALDOCUMENT_QT_PREFIX:-${HOME}/Qt/6.8.3/macos}"

# A moved source tree invalidates all generated paths, including ExternalProject
# dependency build trees, so regenerate the complete build directory.
if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    cached_source="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${BUILD_DIR}/CMakeCache.txt")"
    cached_build="$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "${BUILD_DIR}/CMakeCache.txt")"
    if [[ "${cached_source}" != "${ROOT_DIR}" || "${cached_build}" != "${BUILD_DIR}" ]]; then
        echo "Removing stale CMake build directory: ${BUILD_DIR}"
        rm -rf "${BUILD_DIR}" || rm -rf "${BUILD_DIR}"
    fi
fi

cmake --fresh -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
    -DIIGENERALDOCUMENT_LOCAL_LIBRARY_ROOT="${SDK_ROOT}" \
    -DCMAKE_PREFIX_PATH="${QT_PREFIX};${SDK_ROOT}/iiXml;${SDK_ROOT}/iiHtmlBlock" \
    -DBUILD_TESTING=ON
cmake --build "${BUILD_DIR}" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
ctest --test-dir "${BUILD_DIR}" --output-on-failure
cmake --install "${BUILD_DIR}" --prefix "${PREFIX}"

# Validate the actual installed package and its automatic SDK dependency lookup.
cmake --fresh -S "${ROOT_DIR}/tests/consumer" -B "${BUILD_DIR}/installed-consumer" -G Ninja \
    -DCMAKE_PREFIX_PATH="${PREFIX};${QT_PREFIX}" \
    -DCMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH=OFF \
    -DIIGENERALDOCUMENT_LOCAL_LIBRARY_ROOT="${SDK_ROOT}"
cmake --build "${BUILD_DIR}/installed-consumer" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
"${BUILD_DIR}/installed-consumer/iiGeneralDocumentConsumer"
echo "iiGeneralDocument installed and verified: ${PREFIX}"
