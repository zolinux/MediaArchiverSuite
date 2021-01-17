#ifndef __IFILECOPIERLINUX_HPP__
#define __IFILECOPIERLINUX_HPP__

#include "IFileCopier.hpp"

class FileCopierLinux : public IFileCopier
{
public:
  FileCopierLinux();
  ~FileCopierLinux() = default;

  void copyFile(
    const char *src, const char *dst, const timespec *const mtime) override;
  void moveFile(
    const char *src, const char *dst, const timespec *const mtime) override;
  void getFileTimes(const char *src, timespec mtime[2]) override;
  void setFileTimes(const char *src, timespec mtime[2]) override;
  size_t getFileSize(const char *src) override;
};

#endif // !__IFILECOPIERLINUX_HPP__