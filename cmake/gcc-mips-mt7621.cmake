if(__PLATFORM_GENERIC_GNU)
  return()
endif()
set(__PLATFORM_GENERIC_GNU 1)

set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR 1004kc)
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_EXECUTABLE_SUFFIX "")

set(
  CMAKE_EXE_LINKER_FLAGS_INIT
  " --sysroot=${ROOTFS_DIR} -Wl,--gc-sections,--print-memory-usage -Wl,-rpath=/lib:. -Wl,--dynamic-linker=/lib/ld-musl-mipsel.so.1"
)
set(
  CMAKE_EXE_LINKER_FLAGS
  ${CMAKE_EXE_LINKER_FLAGS_INIT}
)

set(CMAKE_EXE_LINKER_FLAGS_RELEASE " -Wl,--strip-all")
set(CMAKE_EXE_LINKER_FLAGS_MINSIZEREL " -Wl,--strip-all")

macro(__platform_generic_gnu lang)
  string(APPEND CMAKE_${lang}_FLAGS
    " -march=${CMAKE_SYSTEM_PROCESSOR} -mmusl -fPIC")

  string(APPEND
    CMAKE_${lang}_FLAGS
    " -ffunction-sections -fdata-sections -fno-strict-aliasing -fno-common -Wno-unused-result"
  )
  string(APPEND
    CMAKE_${lang}_FLAGS
    " -Wl,--warn-common"
  )

  string(APPEND CMAKE_${lang}_FLAGS_DEBUG " -DDEBUG -O0 -ggdb3")
  string(APPEND CMAKE_${lang}_FLAGS_MINSIZEREL " -DNDEBUG -Os -s")
  string(APPEND CMAKE_${lang}_FLAGS_RELEASE " -DNDEBUG -O3 -s")
  string(APPEND CMAKE_${lang}_FLAGS_RELWITHDEBINFO " -DNDEBUG -O3 -s -ggdb3")

  set(CMAKE_${lang}_OUTPUT_EXTENSION ".o")
  set(CMAKE_${lang}_OUTPUT_EXTENSION_REPLACE 1)
endmacro()

set(CMAKE_SKIP_RPATH TRUE)
SET(CMAKE_FIND_ROOT_PATH /home/zoli/work/openwrt-sdk/staging_dir/target-mipsel_24kc_musl/usr;/home/zoli/work/openwrt-sdk/staging_dir/toolchain-mipsel_24kc_gcc-8.4.0_musl)

SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BOTH)
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

__platform_generic_gnu(C)
__platform_generic_gnu(CXX)
__platform_generic_gnu(ASM)


