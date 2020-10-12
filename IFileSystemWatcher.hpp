#ifndef __IFILESYSTEMWATCHER_HPP__
#define __IFILESYSTEMWATCHER_HPP__

#include <string>
#include <list>

namespace MediaArchiver
{
class IFileSystemWatcher
{
public:
  virtual void start() = 0;
  virtual void stop() = 0;

  virtual ~IFileSystemWatcher(){};
};
}
#endif