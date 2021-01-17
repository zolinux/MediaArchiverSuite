#ifndef __IFILECOPIER_HPP__
#define __IFILECOPIER_HPP__

#include <stddef.h>
struct timespec;

class IFileCopier
{
public:
  virtual void copyFile(
    const char *src, const char *dst, const timespec *const mtime) = 0;
  virtual void moveFile(
    const char *src, const char *dst, const timespec *const mtime) = 0;
  virtual void getFileTimes(const char *src, timespec mtime[2]) = 0;
  virtual void setFileTimes(const char *src, timespec mtime[2]) = 0;
  virtual size_t getFileSize(const char *src) = 0;
  virtual ~IFileCopier() = default;
};

#endif // !__IFILECOPIER_HPP__