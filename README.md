# MediaArchiverSuite
Video converter server + client, where server is minimalistic to be run on a router or any embedded device

# Quickstart

The project was developed for Linux and Windows/MinGW

## Building the application

The Toolchain file `cmake/gcc-mips-mt7621.cmake` is an example toolchain file to compile the application for a MIPS 1004kc based router running [OpenWRT](https://openwrt.org/). The toolchain (GCC, G++, etc) should be [downloaded](https://downloads.openwrt.org/releases/19.07.4/targets/ramips/mt7621/openwrt-sdk-19.07.4-ramips-mt7621_gcc-7.5.0_musl.Linux-x86_64.tar.xz) from their website. Executing the following lines will build the **MediaArchiverDaemon** that will be run on the router:

```bash
#!/bin/sh
mkdir build && cd build

rootdir=/home/zoli/work/openwrt-sdk-19.07.4-ramips-mt7621_gcc-7.5.0_musl.Linux-x86_64/staging_dir/target-mipsel_24kc_musl/root-ramips/usr
toolchaindir=/home/zoli/work/openwrt-sdk-19.07.4-ramips-mt7621_gcc-7.5.0_musl.Linux-x86_64/staging_dir/toolchain-mipsel_24kc_gcc-7.5.0_musl/bin

cmake -DCMAKE_C_COMPILER="$toolchaindir/mipsel-openwrt-linux-gcc" -DCMAKE_CXX_COMPILER="$toolchaindir/mipsel-openwrt-linux-g++" -DCMAKE_TOOLCHAIN_FILE=~/work/MediaArchiverSuite/cmake/gcc-mips-mt7621.cmake -DROOTFS_DIR=$rootdir -DCMAKE_BUILD_TYPE=MinSizeRel -G Ninja -S ../MediaArchiverSuite -B .
cmake --build . --target MediaArchiverDaemon
```

The other target `MediaArchiverClient` is typically run on a powerful machine either Linux or Windows. Since the codebase uses POSIX-like headers, it will **not** compile using MSVC, instead, GCC and [MinGW](http://mingw-w64.org/) environment is required. To build it on a regular PC, usually no toolchain file is needed.

## Running the server

The configuration file `MediaArchiver.cfg` needs to be adapted to the environment (folders containing the video files, logging, etc), then launching the `MediaArchiverDaemon` will create the initial database file (if not yet present any), generate a catalog of the media found and after that the service will be started.

## Running the client

Running the `MediaArchiverClientMain` on multiple machines at the same time leverages the computing power, so that many file can be transcoded in parallel. A functional FFMPEG executable is needed which will be executed in a separate process for the encoding. 

## Security

There are no security checks and encryption built in, the service and the client shall **not** be accessible from outside the internal network.

# Disclaimer

No warranty ever, use it at your own risk.
Please read the [LICENSE](LICENSE) file as well.