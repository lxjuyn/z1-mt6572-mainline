#!/usr/bin/env bash
# Build Z1 LOS16 targets with workspace-local legacy host tools.
set -o pipefail

ANDROID9_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
LOS_ROOT=$ANDROID9_ROOT/los16
export JAVA_HOME=$LOS_ROOT/prebuilts/jdk/jdk9/linux-x86
export PATH=$ANDROID9_ROOT/host-tools/python2/bin:$JAVA_HOME/bin:$ANDROID9_ROOT/bin:$PATH
# Z1_KERNEL_VARIANT=stock selects Binder protocol 7 and legacy board init.
export Z1_KERNEL_VARIANT=${Z1_KERNEL_VARIANT:-mainline}
case "$Z1_KERNEL_VARIANT" in mainline|stock) ;; *) echo "Invalid Z1_KERNEL_VARIANT" >&2; exit 2;; esac
export GOCACHE=$LOS_ROOT/.cache/go-build
export LD_LIBRARY_PATH=$ANDROID9_ROOT/host-tools/compat-root/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
mkdir -p "$GOCACHE"

cd "$LOS_ROOT" || exit 1
source build/envsetup.sh || exit 1
lunch lineage_z1-userdebug || exit 1
m -j"${BUILD_JOBS:-12}" "$@"
