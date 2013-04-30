#!/usr/bin/env python3

from distutils.core import setup, Extension
import glob
import os

packages = ['glib-2.0', 'jack', 'fluidsynth', 'libusb-1.0', 'smf', 'sndfile']

eargs = os.popen("pkg-config --cflags %s" % (" ".join(packages)), "r").read().split()
eargs.append("-std=c99")

libs = os.popen("pkg-config --libs %s" % (" ".join(packages)), "r").read().split()
libs.append("-luuid")

csources = [
    "app.c",
    "auxbus.c",
    "blob.c",
    "chorus.c",
    "cmd.c",
    "compressor.c",
    "config-api.c",
    "delay.c",
    "distortion.c",
    "dom.c",
    "eq.c",
    "engine.c",
    "errors.c",
    "fbr.c",
    "fluid.c",
    "fuzz.c",
    "fxchain.c",
    "gate.c",
    "hwcfg.c",
    "instr.c",
    "io.c",
    "jackinput.c",
    "jackio.c",
    "layer.c",
    "master.c",
    "meter.c",
    "midi.c",
    "mididest.c",
    "module.c",
    "pattern.c",
    "pattern-maker.c",
    "phaser.c",
    "prefetch_pipe.c",
    "recsrc.c",
    "reverb.c",
    "rt.c",
    "sampler.c",
    "sampler_channel.c",
    "sampler_gen.c",
    "sampler_layer.c",
    "sampler_prg.c",
    "sampler_voice.c",
    "scene.c",
    "scripting.c",
    "seq.c",
    "sfzloader.c",
    "sfzparser.c",
    "song.c",
    "streamplay.c",
    "streamrec.c",
    "tonectl.c",
    "tonewheel.c",
    "track.c",
    "usbaudio.c",
    "usbio.c",
    "usbmidi.c",
    "usbprobe.c",
    "wavebank.c"
]

setup(name="CalfBox",
    version="0.04", description="Assorted music-related code", 
    author="Krzysztof Foltman", author_email="wdev@foltman.com",
    url="http://repo.or.cz/w/calfbox.git", 
    packages=["calfbox"],
    package_dir={'calfbox':'py'},
    ext_modules=[
        Extension('_cbox', csources, 
            extra_compile_args = eargs,
            include_dirs=['.'],
            extra_link_args=libs,
            define_macros=[("_GNU_SOURCE","1"),("_POSIX_C_SOURCE", "199309L"),("USE_PYTHON","1"),("CALFBOX_AS_MODULE", "1")],
            undef_macros=['NDEBUG'],
            depends = ['setup.py']
        )
    ],
)
  