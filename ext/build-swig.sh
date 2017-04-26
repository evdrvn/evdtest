#!/bin/sh
rm -rf swig-3.0.12
tar -xzvf swig-3.0.12.tar.gz
sh ./pcre-build.sh
cd swig-3.0.12
swig_dir=`pwd`
./configure --prefix=$swig_dir
make
make install
cd ..

