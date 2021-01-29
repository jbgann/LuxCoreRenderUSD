#!/bin/bash

export USD_ROOT=~/masters/USD
export LUXCORE_ROOT=~/masters/LuxCore-opencl-sdk
export LUXCORERENDERUSD_REPO=~/masters/LuxCoreRenderUSD

rm -rf build
cmake -B build .
cd build
cmake --build . --config Release -- VERBOSE=1
cd ..

# Copy the plugin library
cp $LUXCORERENDERUSD_REPO/build/pxr/imaging/plugin/hdLuxCore/*.so ~/masters/USD/plugin/usd

# LuxCore uses a different version of TBB and this matters on Linux
cp $LUXCORE_ROOT/lib/libtbb.so.2 $USD_ROOT/lib/libtbb.so.2
