#!/usr/bin/env bash
set -euo pipefail

DOCUMENT_SOURCE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOCUMENT_BUILD="${DOCUMENT_SOURCE}/build"
DOCUMENT_PREFIX="${IIGENERALDOCUMENT_INSTALL_PREFIX:-${HOME}/.local/SDK/iiGeneralDocument}"
DOCUMENT_SDK_ROOT="${IIGENERALDOCUMENT_LOCAL_LIBRARY_ROOT:-${HOME}/.local/SDK}"
NATIVE_PROVIDER_SOURCE="${DOCUMENT_SOURCE}/../iiFileProvider/Native"
NATIVE_PROVIDER_BUILD="${DOCUMENT_SOURCE}/../iiFileProvider/build/native"
DOCUMENT_PARALLELISM="${CMAKE_BUILD_PARALLEL_LEVEL:-4}"

# Preserve generated evidence and existing SDK libraries; a moved build must be
# explicitly relocated before reuse, rather than recursively deleting build/.
cmake -S "${NATIVE_PROVIDER_SOURCE}" -B "${NATIVE_PROVIDER_BUILD}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${DOCUMENT_SDK_ROOT}/iiFileProvider" \
    -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=TRUE \
    -DCMAKE_DISABLE_FIND_PACKAGE_Qt=TRUE -DBUILD_TESTING=ON
cmake --build "${NATIVE_PROVIDER_BUILD}" --parallel "${DOCUMENT_PARALLELISM}"
ctest --test-dir "${NATIVE_PROVIDER_BUILD}" --output-on-failure
cmake --install "${NATIVE_PROVIDER_BUILD}"

cmake -S "${DOCUMENT_SOURCE}" -B "${DOCUMENT_BUILD}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="${DOCUMENT_PREFIX}" \
    -DIIGENERALDOCUMENT_LOCAL_LIBRARY_ROOT="${DOCUMENT_SDK_ROOT}" \
    -DCMAKE_PREFIX_PATH="${DOCUMENT_SDK_ROOT}/iiFileProvider;${DOCUMENT_SDK_ROOT}/iiXml" \
    -DIIGENERALDOCUMENT_SANITIZERS=OFF -DIIGENERALDOCUMENT_PDF_SANITIZERS=OFF \
    -DIIGENERALDOCUMENT_PDF_ONLY=OFF -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE \
    -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=TRUE -DCMAKE_DISABLE_FIND_PACKAGE_Qt=TRUE \
    -DCMAKE_DISABLE_FIND_PACKAGE_iiFileProvider=TRUE \
    -DCMAKE_DISABLE_FIND_PACKAGE_iiXml=TRUE -DCMAKE_DISABLE_FIND_PACKAGE_iiHtmlBlock=TRUE \
    -DBUILD_TESTING=ON
cmake --build "${DOCUMENT_BUILD}" --parallel "${DOCUMENT_PARALLELISM}"
ctest --test-dir "${DOCUMENT_BUILD}" --output-on-failure
cmake --install "${DOCUMENT_BUILD}" --prefix "${DOCUMENT_PREFIX}"

# Compile/run the actual installed package with all legacy package searches off.
cmake --fresh -S "${DOCUMENT_SOURCE}/tests/consumer" \
    -B "${DOCUMENT_BUILD}/installed-consumer" -G Ninja \
    -DCMAKE_PREFIX_PATH="${DOCUMENT_PREFIX}" \
    -DCMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH=OFF \
    -DIIGENERALDOCUMENT_LOCAL_LIBRARY_ROOT="${DOCUMENT_SDK_ROOT}"
cmake --build "${DOCUMENT_BUILD}/installed-consumer" --parallel "${DOCUMENT_PARALLELISM}"
(cd "${DOCUMENT_BUILD}/installed-consumer" && \
    env -u DYLD_LIBRARY_PATH -u DYLD_FALLBACK_LIBRARY_PATH ./iiGeneralDocumentConsumer)
env -u DYLD_LIBRARY_PATH -u DYLD_FALLBACK_LIBRARY_PATH \
    ctest --test-dir "${DOCUMENT_BUILD}/installed-consumer" --output-on-failure
printf 'iiGeneralDocument installed and verified: %s\n' "${DOCUMENT_PREFIX}"
