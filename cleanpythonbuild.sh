#!/bin/bash
set -e
make clean
rm build -rf
sh autogen.sh
./configure
make
python3 setup.py build
sudo python3 setup.py install
