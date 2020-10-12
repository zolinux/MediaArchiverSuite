#ifndef __FILESYSTEMWATCHERLINUX_HPP__
#define __FILESYSTEMWATCHERLINUX_HPP__

#include <thread>
#include <utility>
#include <chrono>
#include <unordered_map>

#include "IFileSystemWatcher.hpp"
#include "IFileSystemChangeListener.hpp"

struct inotify_event;
namespace MediaArchiver
{
class FileSystemWatcherLinux : public IFileSystemWatcher
{
public:
  FileSystemWatcherLinux(IFileSystemChangeListener &listener,
    const std::list<std::string> &folders);
  FileSystemWatcherLinux(const FileSystemWatcherLinux &) = delete;
  FileSystemWatcherLinux(FileSystemWatcherLinux &&) = default;
  virtual void start() override;
  virtual void stop() override;

  virtual ~FileSystemWatcherLinux();

private:
  static const uint32_t InotifyFlags;
  static const std::chrono::milliseconds MoveTimeout;
  void threadMain();
  void handleInotifyEvent(const struct inotify_event *e);
  void watchDir(const std::string &dirName);
  int m_fd;
  int m_eventfd;
  bool m_stopping;
  bool m_moveFrom;
  uint32_t m_cookie;
  std::string m_moveName;
  std::unordered_map<int, std::string> m_watchedDirs;
  std::chrono::steady_clock::time_point m_moveStart;
  IFileSystemChangeListener &m_dst;
  std::unique_ptr<std::thread> m_pollerThread;
};
}
#endif