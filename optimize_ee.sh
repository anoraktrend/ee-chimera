#!/bin/sh
set -e

# This script optimizes the ee binary using LLVM Propeller

# Extract current CFLAGS and LDFLAGS from config.mk or use defaults
if [ -f config.mk ]; then
    CUR_CFLAGS=$(grep "^CFLAGS =" config.mk | cut -d'=' -f2-)
    CUR_LDFLAGS=$(grep "^LDFLAGS =" config.mk | cut -d'=' -f2-)
else
    CUR_CFLAGS="-std=c23 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600"
    CUR_LDFLAGS="-lcurses"
fi

# 1. Build with basic block address map
echo "Building with basic block address map..."
make clean
make CFLAGS="-O3 -fbasic-block-address-map $CUR_CFLAGS" LDFLAGS="-fuse-ld=lld $CUR_LDFLAGS"

# 2. Profile the binary
echo "Profiling..."
# Try to record with LBR if supported, otherwise fallback
if perf record -e cycles:u -j any,u -o perf.data -- true > /dev/null 2>&1; then
    PERF_FLAGS="-e cycles:u -j any,u"
elif perf record -e br_inst_retired.near_taken:u -j any,u -o perf.data -- true > /dev/null 2>&1; then
    PERF_FLAGS="-e br_inst_retired.near_taken:u -j any,u"
else
    echo "Warning: LBR not supported, falling back to basic profiling"
    PERF_FLAGS="-e cycles:u"
fi

perf record $PERF_FLAGS -o perf.data -- ./ee -i << EOF
:read ee.c
:line
:down
:right
:search search
:quit
EOF

# 3. Convert profiles
echo "Converting profiles..."
if which create_llvm_prof > /dev/null 2>&1; then
    create_llvm_prof --format=propeller --binary=./ee --profile=perf.data --out=cluster.txt --propeller_symorder=symorder.txt
else
    echo "Error: create_llvm_prof (autofdo) not found. Cannot continue Propeller optimization."
    exit 1
fi

# 4. Re-build with Propeller profiles
echo "Building Propeller optimized binary..."
if [ -f cluster.txt ]; then
    make clean
    make CFLAGS="-O3 -fbasic-block-sections=list=cluster.txt $CUR_CFLAGS" \
         LDFLAGS="-Wl,--symbol-ordering-file=symorder.txt -Wl,--no-warn-symbol-ordering -fuse-ld=lld $CUR_LDFLAGS"
    echo "Propeller optimization complete."
else
    echo "Error: cluster.txt not generated. Optimization failed."
    exit 1
fi
