#!/bin/bash
make clean
rm build -rf
set -e
sh autogen.sh
./configure
#make
python3 setup.py build
sudo python3 setup.py install
