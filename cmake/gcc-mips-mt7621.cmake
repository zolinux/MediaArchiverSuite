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
  " -Wl,--gc-sections,--print-memory-usage -Wl,-rpath=sysroot/libsubdir:${ROOTFS_DIR} -Wl,--dynamic-linker=sysroot/libsubdir/lib/ld-uClibc.so.0"
)
set(
  CMAKE_EXE_LINKER_FLAGS
  ${CMAKE_EXE_LINKER_FLAGS_INIT}
)

set(CMAKE_EXE_LINKER_FLAGS_RELEASE " -Wl,--strip-all")
set(CMAKE_EXE_LINKER_FLAGS_MINSIZEREL " -Wl,--strip-all")

macro(__platform_generic_gnu lang)
  string(APPEND CMAKE_${lang}_FLAGS
    " -march=${CMAKE_SYSTEM_PROCESSOR} -muclibc")

  # string(APPEND
  #   CMAKE_${lang}_FLAGS_INIT
  #   " -Wall -Wextra -pedantic -Werror"
  # )
  string(APPEND
    CMAKE_${lang}_FLAGS
    " -ffunction-sections -fdata-sections -fno-strict-aliasing -fno-common"
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

SET(CMAKE_FIND_ROOT_PATH ${ROOTFS_DIR})
SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

__platform_generic_gnu(C)
__platform_generic_gnu(CXX)
__platform_generic_gnu(ASM)


