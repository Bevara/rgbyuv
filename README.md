# rgbyuv
This filter converts packed RGB frames to planar YUV 4:2:0, BT.601 at studio
range.

## Why it exists

Every still-image decoder in this build hands back packed RGB - libjpeg,
openjpeg, libpng, libjxr and the rest all do, because that is what `writegen`
wants when the output is an image. It is not what an encoder wants: x264 takes
planar YUV. A chain that decodes frames and re-encodes them to MP4 - Motion
JPEG is exactly that - therefore has a hole in the middle where a colour
conversion should be, and this filter fills it.

It is declared `GF_CAPFLAG_RECONFIG`, so the session can load it by itself when
the two ends of a link disagree on the pixel format, rather than having to be
named in the graph.

## What to know about the conversion

BT.601 studio range - 16..235 for luma, 16..240 for chroma - which is what
H.264 assumes when a stream says nothing else. JPEG carries full-range YCbCr
internally, so a frame that came from a JPEG has been expanded to RGB by the
decoder and is narrowed again here; that round trip costs a little dynamic
range and is the conventional thing to do.

Chroma is taken from the top-left pixel of each 2x2 block rather than averaged:
the four values a 4:2:0 JPEG gives back come from one chroma sample to begin
with, so averaging would blur what was never sharp.

Verified against ffmpeg's equivalent path (JPEG to RGB, then RGB to studio-range
YUV 4:2:0): 48.8 dB PSNR through a full decode/convert/x264 chain, which is
x264's own re-encoding loss.

## Requirements

[CMake](https://cmake.org/) is used as a build system. To install it, follow
[Debian build instructions](developing_in_debian.md).

[Emscripten SDK](https://emscripten.org/) is required for building
WebAssembly artifacts. To install it, follow the
[Download and Install](https://emscripten.org/docs/getting_started/downloads.html)
guide:

```bash
cd $OPT

# Get the emsdk repo.
git clone https://github.com/emscripten-core/emsdk.git

# Enter that directory.
cd emsdk

# Download and install the latest SDK tools.
./emsdk install latest

# Make the "latest" SDK "active" for the current user. (writes ~/.emscripten file)
./emsdk activate latest
```

## Building the accessor

```bash
# Setup EMSDK and other environment variables. In practice EMSDK is set to be
# $OPT/emsdk.
source $OPT/emsdk/emsdk_env.sh

# Assuming you are in the root level of the cloned repo :
emcmake cmake .
emmake make
```

Once built, you can use and distribute rgbyuv_1.wasm with your universal tags.

## Documentation

For more details, please visit our documentation at https://bevara.com/documentation/develop/.
