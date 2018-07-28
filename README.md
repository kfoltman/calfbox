#Calfbox

Website: https://github.com/kfoltman/calfbox

Calfbox, the "open source musical instrument", offers assorted music-related code.

Originally intended as a standalone instrument for Linux and embedded devices (USB TV Sticks)
it can be used as Python module as well.

#Calfbox as Python Module
Calfbox can be used as a Python module that can be imported to create short scripts or
full fledged programs ( https://www.laborejo.org/software ).

Most notably it features a midi sequencer and an audio sampler (for sfz files and sf2 via fluidsynth).

## Building

A convenience script `cleanpythonbuild.py` has been supplied to quickly build and install the cbox python module.

```
make clean
rm build -rf
sh autogen.sh
./configure
make
python3 setup.py build
sudo python3 setup.py install
```

## How to write programs with cbox
You can find several `.py` files in the main directory, such as `sampler_api_example.py` or
`song_api_example.py`.

Also there is a directory `/experiments` which contains a small example framework.


#Using Calfbox as standalone instrument

Using Calfbox as standalone instrument requires a .cfg config file.

TODO

# License

This code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

For the full license see the file COPYING
