#!/bin/bash
# Presubmit CI script for CHIP-SPV/rocPRIM fork.
# Mirrors the shape of chipStar's scripts/unit_tests.sh. Runs on the
# [self-hosted, Linux, X64] runner against a pre-installed chipStar module.
#
# Usage: ci_unit_tests.sh <debug|release> llvm-22 --variant=<native|translator>
#
# Required modules on the runner:
#   llvm/22.0-<variant>
#   HIP/chipStar/ci-llvm22-<variant>-<build_type>    (chipStar install matching
#                                                     the LLVM variant+build type)
#   oneapi/2025.0.4, level-zero/dgpu
#
# Runner admins: point the HIP/chipStar/ci-llvm22-* modules at a chipStar
# install built from a recent main; the rocPRIM CI will rebuild rocPRIM
# against it on every PR.

set -e

if [ "$#" -lt 3 ]; then
  echo "Usage: $0 <debug|release> llvm-22 --variant=<native|translator>"
  exit 1
fi

build_type_lc="$1"
llvm_arg="$2"
variant_arg="$3"

case "$build_type_lc" in
  debug|release) ;;
  *) echo "Error: first arg must be 'debug' or 'release'"; exit 1;;
esac

if [ "$llvm_arg" != "llvm-22" ]; then
  echo "Error: only llvm-22 is supported"; exit 1
fi

case "$variant_arg" in
  --variant=native|--variant=translator) ;;
  *) echo "Error: third arg must be --variant=native or --variant=translator"; exit 1;;
esac

variant="${variant_arg#--variant=}"
build_type=$(echo "$build_type_lc" | tr '[:lower:]' '[:upper:]')

echo "build_type = $build_type"
echo "variant    = $variant"

# Source environment modules.
if [ -f "$HOME/.local/init/bash" ]; then
  export MODULESHOME=$HOME/.local
  source "$MODULESHOME/init/bash"
elif [ -f /etc/profile.d/lmod.sh ]; then
  source /etc/profile.d/lmod.sh &> /dev/null
else
  source /etc/profile.d/modules.sh &> /dev/null
fi

module use ~/modulefiles
module load oneapi/2025.0.4
module load "llvm/22.0-${variant}"
module load "HIP/chipStar/ci-llvm22-${variant}-${build_type_lc}"
module load level-zero/dgpu
module list

unset CHIP_PLATFORM
unset CHIP_DEVICE
export CHIP_LOGLEVEL=err
export CHIP_MODULE_CACHE_DIR=""
rm -rf ~/.cache/chipStar

hipcc_bin="$(which hipcc)"
echo "hipcc = $hipcc_bin"

rm -rf build
mkdir build
cd build

cmake .. \
  -DCMAKE_CXX_COMPILER="$hipcc_bin" \
  -DCMAKE_BUILD_TYPE="$build_type" \
  -DBUILD_TEST=ON \
  -DBUILD_BENCHMARK=OFF \
  -DAMDGPU_TEST_TARGETS=""

# rocPRIM translation units are heavy; cap parallelism.
nproc_capped=$(( $(nproc) < 8 ? $(nproc) : 8 ))
cmake --build . -j "$nproc_capped"

exclude_regex=""
if [ -f ../.ci/exclude.txt ]; then
  # Strip comments/blank lines, join with '|'.
  exclude_regex=$(grep -Ev '^\s*(#|$)' ../.ci/exclude.txt | paste -sd '|' -)
fi

ctest_args=(--output-on-failure --timeout 300 -L chip-spv-gpu)
if [ -n "$exclude_regex" ]; then
  ctest_args+=(-E "$exclude_regex")
fi

echo "ctest ${ctest_args[*]}"
ctest "${ctest_args[@]}"
