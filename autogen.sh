#! /bin/bash
set -ex

if [[ -x "$(command -v realpath)" ]]; then
  ROOTDIR="$(realpath $(dirname $0))"
else
  ROOTDIR="$(dirname $0)"
fi

BUILD_TYPE="${1:-Debug}"
WORKDIR="build"
if [[ "${BUILD_TYPE}" != "Debug" ]]; then
    WORKDIR="release"
fi

mkdir -p "${ROOTDIR}/${WORKDIR}"
cd "${ROOTDIR}/${WORKDIR}"

exec cmake "${ROOTDIR}" \
           -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
           -DCMAKE_CXX_FLAGS="-fdiagnostics-color=always" \
           -GNinja

