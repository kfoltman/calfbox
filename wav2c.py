import argparse
import array
import sys
import wave

def u2s(value):
    return value if value < 32768 else value - 65536

parser = argparse.ArgumentParser(description="Convert a WAV file into a C function")
parser.add_argument('input', type=str, help="WAV file to convert")
parser.add_argument('function', type=str, help="Function to generate")
parser.add_argument('name', type=str, help="Name for registering the waveform in the wavebank")

args = parser.parse_args()
name = args.name
funcname = args.function
f = wave.open(args.input, "rb")
channels = f.getnchannels()
width = f.getsampwidth()
rate = f.getframerate()
frames = f.getnframes()
data = f.readframes(frames)
f.close()

if width == 2:
    data = array.array("h", data)
elif width == 3:
    # Convert 24 bits to 16
    idata = array.array("B", data)
    data = []
    for i in range(0, len(idata), 3):
        data.append(u2s(idata[i + 1] + 256 * idata[i + 2]))
else:
    sys.stderr.write(f"Unexpected data format - {8 * width} bits instead of 16\n")
    sys.exit(1)

data = list(data)

print ("static short sample_data[] = {")
for i in range(0, frames, 16):
    print (repr(data[i : i + 16])[1:-1] + ",")

# extern struct cbox_waveform *cbox_wavebank_add_mem_waveform(const char *name, void *data, uint32_t frames, int sample_rate, int channels, gboolean looped, uint32_t loop_start, uint32_t loop_end);

args = f'{repr(name)}, sample_data, {frames}, {rate}, {channels}, 0, 0, 0'

print ("""\
}

struct cbox_waveform *FUNCTION(void) {
    cbox_wavebank_add_mem_waveform(ARGS);
}
""".replace("ARGS", args).replace("FUNCTION", funcname))

