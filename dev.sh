#!/bin/bash
export MARI_PLUGINS_PATH=/USERS/chichang/workspace/MriExrImageWriterPlugin/build
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/X/tools/packages/gcc-4.1/oiio/oiio-1.4.9/lib
# launch mari
mari $@
