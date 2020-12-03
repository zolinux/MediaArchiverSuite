#include "FileCopierLinux.hpp"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <sstream>
#include <stdexcept>

FileCopierLinux::FileCopierLinux() {}

void FileCopierLinux::copyFile(
  const char *src, const char *dst, const timespec *const mtime)
{
  timespec ts[2];

  struct stat finfo;

  int s = open(src, O_RDONLY);
  if(s < 0)
    throw std::runtime_error("Could not open file");

  auto st = fstat(s, &finfo);
  auto fsize = lseek(s, 0, SEEK_END);
  if(fsize == -1)
  {
    throw std::runtime_error("Could not seek in file");
  }

  lseek(s, 0, SEEK_SET);

  posix_fadvise(s, 0, 0, POSIX_FADV_SEQUENTIAL);

  if(mtime)
  {
    ts[0] = *mtime;
    ts[1] = *mtime;
  }
  else
  {
    ts[0] = finfo.st_atim;
    ts[1] = finfo.st_mtim;
  }
  
  int d = open(dst, O_WRONLY | O_CREAT | O_TRUNC, &ts[0]);
  posix_fallocate(d, 0, fsize);

  char buf[8192];
  while(true)
  {
    auto rs = read(s, buf, sizeof(buf));
    if(!rs)
      break;

    auto rs2 = write(d, buf, sizeof(buf));
    if(rs != rs2)
    {
      close(s);
      close(d);
      throw std::runtime_error("IO error during copying the file");
    }
  }

  close(d);

  futimens(d, ts);
  close(s);
}

void FileCopierLinux::moveFile(
  const char *src, const char *dst, const timespec *const mtime)
{
  std::stringstream ss;
  ss << "mv \"" << src << "\" \"" << dst << "\"";

  int i = system(ss.str().c_str());
  if(i)
  {
    throw std::runtime_error(
      std::string("Error during moving file: ") + ss.str());
  }

  timespec ts[2];
  int res = 0;
  if(mtime)
  {
    ts[0] = *mtime;
    ts[1] = *mtime;
  }
  else
  {
    getFileTimes(src, ts);
  }

  res = utimensat(AT_FDCWD, dst, ts, 0);
  if(res)
  {
    throw std::runtime_error(std::string("Error modifying time: ") + dst);
  }
}

void FileCopierLinux::getFileTimes(const char *src, timespec mtime[2])
{
  struct stat finfo;
  int res;
  res = stat(src, &finfo);

  if(res)
  {
    throw std::runtime_error(std::string("Error stat: ") + src);
  }
  mtime[0] = finfo.st_atim;
  mtime[1] = finfo.st_mtim;
}

size_t FileCopierLinux::getFileSize(const char *src)
{
  struct stat finfo;
  int res;
  res = stat(src, &finfo);

  if(res)
  {
    throw std::runtime_error(std::string("Error stat: ") + src);
  }

  return finfo.st_size;
}
