#!/bin/bash
make clean
make distclean
rm build -rf
set -e
sh autogen.sh
./configure --prefix=/usr
make
python3 setup.py build
sudo python3 setup.py install
sudo make install
