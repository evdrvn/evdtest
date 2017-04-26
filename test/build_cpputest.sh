#!/bin/sh
cd ext/cpputest-3.8
rm -rf cpputest_build/*
cd cpputest_build
cmake -DCMAKE_INSTALL_PREFIX=.. -DCOVERAGE=ON ..
make
make install
cd ../../..

