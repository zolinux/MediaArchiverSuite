if(__PLATFORM_GENERIC_GNU)
  return()
endif()
set(__PLATFORM_GENERIC_GNU 1)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_EXECUTABLE_SUFFIX "")

SET(CMAKE_SYSTEM_NAME Linux)
SET(CMAKE_SYSTEM_VERSION 1)

string(APPEND
  CMAKE_EXE_LINKER_FLAGS_INIT
  " -Wl,--gc-sections,--print-memory-usage"
)

set(CMAKE_EXE_LINKER_FLAGS_RELEASE " -Wl,--strip-all")
set(CMAKE_EXE_LINKER_FLAGS_MINSIZEREL " -Wl,--strip-all")

macro(__platform_generic_gnu lang)
  string(APPEND CMAKE_${lang}_FLAGS_INIT
    " -march=1004kc -muclibc")

  # string(APPEND
  #   CMAKE_${lang}_FLAGS_INIT
  #   " -Wall -Wextra -pedantic -Werror"
  # )
  string(APPEND
    CMAKE_${lang}_FLAGS_INIT
    " -ffunction-sections -fdata-sections -fno-strict-aliasing -fno-common"
  )
  string(APPEND
    CMAKE_${lang}_FLAGS_INIT
    " -Wl,--warn-common -Wl,-rpath=sysroot/libsubdir:/mgc/embedded/codebench/mips-linux-gnu/libc/uclibc -Wl,--dynamic-linker=sysroot/libsubdir/lib/ld-uClibc.so.0"
  )
  # string(APPEND
  #   CMAKE_${lang}_FLAGS_INIT
  #   " -Wl,--warn-common -Wl,-rpath=sysroot/libsubdir:/mgc/embedded/codebench/mips-linux-gnu/libc/uclibc -Wl,--dynamic-linker=sysroot/libsubdir/lib/ld-uClibc.so.0"
  # )

  # string(APPEND
  #   CMAKE_${lang}_FLAGS_INIT
  #   " -Wl,--warn-common -Wl,-rpath=sysroot/libsubdir:/Work/archived/mips -Wl,--dynamic-linker=sysroot/libsubdir/ldso"
  # )

  string(APPEND CMAKE_${lang}_FLAGS_DEBUG " -DDEBUG -O0 -ggdb3")
  string(APPEND CMAKE_${lang}_FLAGS_DEBUG_INIT " -O0 -g")
  string(APPEND CMAKE_${lang}_FLAGS_MINSIZEREL " -DNDEBUG -Os -s")
  string(APPEND CMAKE_${lang}_FLAGS_MINSIZEREL_INIT " -Os -s")
  string(APPEND CMAKE_${lang}_FLAGS_RELEASE " -DNDEBUG -O3 -s")
  string(APPEND CMAKE_${lang}_FLAGS_RELEASE_INIT " -O3 -s")
  string(APPEND CMAKE_${lang}_FLAGS_RELWITHDEBINFO " -DNDEBUG -O3 -s -ggdb3")
  string(APPEND CMAKE_${lang}_FLAGS_RELWITHDEBINFO_INIT " -O3 -s -g")

  # string(APPEND CMAKE_LINK_LIBRARY_FLAG " ")

  set(CMAKE_${lang}_OUTPUT_EXTENSION ".o")
  set(CMAKE_${lang}_OUTPUT_EXTENSION_REPLACE 1)
endmacro()

#SET(CMAKE_FIND_ROOT_PATH $ENV{HOME}/raspberrypi/rootfs)
SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

__platform_generic_gnu(C)

__platform_generic_gnu(CXX)
string(APPEND CMAKE_CXX_FLAGS_INIT " -fno-rtti -fno-exceptions -fno-threadsafe-statics")

__platform_generic_gnu(ASM)

