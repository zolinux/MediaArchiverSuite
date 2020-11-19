#ifndef __FILEUTILS_H__
#define __FILEUTILS_H__

#if defined(_MSVC_STL_VERSION)
  #include "FileCopierWindows.hpp"
using FileUtils = FileCopierWindows;
#else
  #include "FileCopierLinux.hpp"
using FileUtils = FileCopierLinux;
#endif

#endif // __FILEUTILS_H__