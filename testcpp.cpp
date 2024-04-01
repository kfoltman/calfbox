
//g++ testcpp.cpp -o testcpp $(pkg-config --cflags --libs glib-2.0)

#include <iostream>

extern "C" {

    #include <glib.h>

    #include "app.h"
    #include "auxbus.h"
    #include "blob.h"
    #include "cmd.h"
    #include "config-api.h"
    #include "dom.h"
    #include "engine.h"
    #include "eq.h"
    #include "errors.h"
    #include "fifo.h"
    #include "hwcfg.h"
    #include "instr.h"
    #include "io.h"
    #include "layer.h"
    #include "master.h"
    #include "meter.h"
    #include "midi.h"
    #include "mididest.h"
    #include "module.h"
    #include "pattern.h"
    #include "pattern-maker.h"
    #include "prefetch_pipe.h"
    #include "recsrc.h"
    #include "rt.h"
    #include "sampler.h"
    #include "sampler_layer.h"
    #include "sampler_prg.h"
    #include "scene.h"
    #include "scripting.h"
    #include "seq.h"
    #include "sfzloader.h"
    #include "sfzparser.h"
    #include "song.h"
    #include "tarfile.h"
    #include "track.h"
    #include "wavebank.h"
}

int main() {
    // Example function call - replace this with actual usage
    std::cout << "Hello World!";
    return 0;
}
