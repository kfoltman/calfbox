#!/usr/bin/env python3

from distutils.core import setup, Extension
import glob
import os
import sys

if sys.version_info[0] < 3:
    raise Exception("Python 3 required.")

packages = ['glib-2.0', 'libusb-1.0', 'smf', 'sndfile']

if '#define USE_FLUIDSYNTH 1' in open('config.h').read():
    packages.append('fluidsynth')
if '#define USE_JACK 1' in open('config.h').read():
    packages.append('jack')

eargs = os.popen("pkg-config --cflags %s" % (" ".join(packages)), "r").read().split()
eargs.append("-std=c99")
# Workaround for Python3.4 headers
eargs.append("-Wno-error=declaration-after-statement")

libs = os.popen("pkg-config --libs %s" % (" ".join(packages)), "r").read().split()
libs.append("-luuid")

csources = [
    "app.c",
    "auxbus.c",
    "blob.c",
    "@chorus.c",
    "cmd.c",
    "@compressor.c",
    "config-api.c",
    "@delay.c",
    "@distortion.c",
    "dom.c",
    "eq.c",
    "engine.c",
    "errors.c",
    "@fbr.c",
    "fifo.c",
    "@fluid.c",
    "@fuzz.c",
    "@fxchain.c",
    "@gate.c",
    "hwcfg.c",
    "instr.c",
    "io.c",
    "@jackinput.c",
    "@jackio.c",
    "layer.c",
    "@limiter.c",
    "master.c",
    "meter.c",
    "midi.c",
    "mididest.c",
    "module.c",
    "pattern.c",
    "pattern-maker.c",
    "@phaser.c",
    "prefetch_pipe.c",
    "recsrc.c",
    "@reverb.c",
    "rt.c",
    "sampler.c",
    "@sampler_channel.c",
    "@sampler_gen.c",
    "sampler_layer.c",
    "sampler_prg.c",
    "@sampler_voice.c",
    "scene.c",
    "scripting.c",
    "seq.c",
    "@seq-adhoc.c",
    "sfzloader.c",
    "sfzparser.c",
    "song.c",
    "@streamplay.c",
    "@streamrec.c",
    "tarfile.c",
    "@tonectl.c",
    "@tonewheel.c",
    "track.c",
    "@usbaudio.c",
    "@usbio.c",
    "@usbmidi.c",
    "@usbprobe.c",
    "wavebank.c",
]

headers = [
    "biquad-float.h",
    "config.h",
    "dspmath.h",
    "envelope.h",
    "ioenv.h",
    "onepole-float.h",
    "onepole-int.h",
    "sampler_impl.h",
    "stm.h",
    "usbio_impl.h",
]

headers += [fn[:-2] + ".h" for fn in csources if fn.endswith(".c") and not fn.startswith("@")]
csources = [fn.lstrip("@") for fn in csources]

if '#define USE_SSE 1' in open('config.h').read():
    eargs.append('-msse')
    eargs.append('-ffast-math')
if '#define USE_NEON 1' in open('config.h').read():
    eargs.append('-mfloat-abi=hard')
    eargs.append('-mfpu=neon')
    eargs.append('-ffast-math')

setup(name="CalfBox",
    version="0.0.0.1", description="Assorted music-related code",
    author="Krzysztof Foltman", author_email="wdev@foltman.com",
    url="https://github.com/kfoltman/calfbox",
    packages=["calfbox"],
    package_dir={'calfbox':'py'},
    ext_modules=[
        Extension('_cbox', csources,
            extra_compile_args = eargs,
            include_dirs=['.'],
            extra_link_args=libs,
            define_macros=[("_GNU_SOURCE","1"),("_POSIX_C_SOURCE", "199309L"),("USE_PYTHON","1"),("CALFBOX_AS_MODULE", "1")],
            undef_macros=['NDEBUG'],
            depends = ['setup.py'] + headers
        )
    ],
)
