#!/bin/sh
set -e

# This script optimizes the ee binary using LLVM Propeller

# 1. Build with basic block address map
echo "Building with basic block address map..."
make clean
make CFLAGS="-O3 -fbasic-block-address-map -std=c23 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600" LDFLAGS="-fuse-ld=lld"

# 2. Profile the binary
# We need a sample workload. We'll simulate some basic editing.
echo "Profiling..."
perf record -e cycles:u -j any,u -- ./ee -i << EOF
:read ee.c
:line
:down
:right
:search search
:quit
EOF

# 3. Convert profiles
echo "Converting profiles..."
# Note: requires create_llvm_prof from autofdo
create_llvm_prof --format=propeller --binary=./ee --profile=perf.data --out=cluster.txt --propeller_symorder=symorder.txt

# 4. Re-build with Propeller profiles
echo "Building Propeller optimized binary..."
make clean
make CFLAGS="-O3 -fbasic-block-sections=list=cluster.txt -std=c23 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600" \
     LDFLAGS="-Wl,--symbol-ordering-file=symorder.txt -Wl,--no-warn-symbol-ordering -fuse-ld=lld"

echo "Propeller optimization complete."
