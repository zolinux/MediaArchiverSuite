
#include <iostream>
#include <istream>
#include <sstream>
#include <sys/inotify.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/select.h>
#include <vector>
#include <string.h>

#include <loguru/loguru.hpp>
#include "FileSystemWatcherLinux.hpp"

namespace
{
const std::string DirWatchRegexPrefix = "regex*";
}

namespace MediaArchiver
{
const uint32_t FileSystemWatcherLinux::InotifyFlags = IN_DELETE |
  IN_DELETE_SELF | IN_MOVE | IN_CLOSE | IN_ONLYDIR;

const std::chrono::milliseconds FileSystemWatcherLinux::MoveTimeout =
  std::chrono::seconds(5);

FileSystemWatcherLinux::FileSystemWatcherLinux(
  IFileSystemChangeListener &listener,
  const std::list<std::string> &folders)
  : m_fd(-1)
  , m_eventfd(-1)
  , m_stopping(false)
  , m_moveFrom(false)
  , m_cookie(0)
  , m_dst(listener)
{
  const int fd = inotify_init1(IN_NONBLOCK);
  if(fd < 0)
  {
    throw std::runtime_error("Error initializing inotify");
  }

  m_fd = fd;
  for(const auto &folder: folders)
  {
    // due to discovering all files, this function takes long time
    // it would be better to do it in thread
    if(!folder.compare(
         0, ::DirWatchRegexPrefix.length(), ::DirWatchRegexPrefix))
    {
      const auto argument = folder.substr(DirWatchRegexPrefix.length());
      const auto separator = argument.find('*');
      if(separator == std::string::npos)
      {
        throw std::runtime_error(
          std::string("regex format invalid: ") + argument);
      }

      const auto startFolder = argument.substr(0, separator);
      const auto re = argument.substr(separator + 1);

      std::stringstream ss;
      ss << "/usr/bin/find " << startFolder << " -type d -regex '" << re
         << "'";
      const std::string command = ss.str();
      std::unique_ptr<FILE> f(popen(command.c_str(), "r"));
      if(!f)
      {
        throw std::runtime_error(
          "Cannot execute command to find the folders");
      }

      char *line = NULL;
      size_t len = 0;
      try
      {
        while(!m_stopping && (getline(&line, &len, f.get())) != -1)
        {
          line[strlen(line) - 1] = 0; // trim line
          watchDir(line);
        }

        if(line)
          free(line);
      }
      catch(const std::exception &e)
      {
        if(line)
          free(line);

        pclose(f.release());
        throw;
      }

      pclose(f.release());

      if(m_stopping)
        return;
    }
    else
    {
      watchDir(folder);
    }
  }
  start();
}

FileSystemWatcherLinux::~FileSystemWatcherLinux()
{
  stop();
  if(m_fd >= 0)
  {
    close(m_fd);
    m_fd = -1;
  }
}

void FileSystemWatcherLinux::watchDir(const std::string &dirName)
{
  struct dir_t
  {
    DIR *dir;
    const std::string name;
  };

  std::vector<dir_t> stack;
  stack.emplace_back(dir_t{nullptr, dirName});

  while(stack.size())
  {
    const struct dirent *entry;
    dir_t &cd = stack.back();

    if(cd.dir == nullptr)
    {
      if(!(cd.dir = opendir(cd.name.c_str())))
      {
        throw std::runtime_error(
          std::string("cannot open directory: ") + cd.name);
      }
      else
      {
        const auto wd = inotify_add_watch(
          m_fd, cd.name.c_str(), FileSystemWatcherLinux::InotifyFlags);
        if(wd < 0)
        {
          throw std::runtime_error(
            std::string("cannot watch directory: ") + cd.name);
        }
        m_watchedDirs[wd] = cd.name;
      }
    }

    while((entry = readdir(cd.dir)) != NULL)
    {
      const auto fullName = cd.name + "/" + entry->d_name;
      if(entry->d_type == DT_REG)
      {
        m_dst.onFileSystemChange(
          IFileSystemChangeListener::EventType::FileDiscovered, "",
          fullName);
      }
      else if(entry->d_type == DT_DIR)
      {
        const std::string name(entry->d_name);
        if(name == "." || name == "..")
          continue;

        stack.emplace_back(dir_t{nullptr, fullName});
        break;
      }
    }
    if(!entry)
    {
      closedir(cd.dir);
      stack.pop_back();
    }
  }
}

void FileSystemWatcherLinux::start()
{
  if(m_eventfd < 0)
  {
    m_eventfd = eventfd(0, /*EFD_SEMAPHORE*/ 0);
  }

  if(m_eventfd < 0)
  {
    throw std::runtime_error("Could not create eventfd");
  }

  m_stopping = false;
  if(!m_pollerThread)
  {
    m_pollerThread.reset(new std::thread([&]() { this->threadMain(); }));
  }
}

void FileSystemWatcherLinux::stop()
{
  if(m_pollerThread)
  {
    // wake up thread to quit
    m_stopping = true;
    uint64_t i = 1;
    write(m_eventfd, &i, sizeof(i));

    m_pollerThread->join();
    m_pollerThread.reset();
    m_stopping = false;
    close(m_eventfd);
    m_eventfd = -1;
  }
}

void FileSystemWatcherLinux::handleInotifyEvent(
  const struct inotify_event *e)
{
  const auto fileName = m_watchedDirs[e->wd] + "/" + e->name;
  if(e->mask & IN_ISDIR)
  {
    // user does not receive any directory-related info, but we process it
    if(e->mask & IN_DELETE)
    {
      LOG_F(4, "directory '%s' was deleted", fileName.c_str());
      inotify_rm_watch(m_fd, e->wd);
      m_watchedDirs.erase(e->wd);
    }
    else if(e->mask & IN_CREATE)
    {
      // directory created. add it to watched list
      const auto ret = inotify_add_watch(
        m_fd, fileName.c_str(), FileSystemWatcherLinux::InotifyFlags);
      if(ret < 0)
      {
        LOG_F(ERROR,
          "directory '%s' could not be added to watched directories: %i",
          fileName.c_str(), ret);
      }
      else
      {
        m_watchedDirs[ret] = fileName;
        LOG_F(4, "directory '%s' was created", fileName.c_str());
      }
    }
    else if(e->mask & IN_UNMOUNT)
    {
      // directory unmounted
      LOG_F(4, "directory '%s' was unmounted", fileName.c_str());
      m_dst.onFileSystemChange(
        IFileSystemChangeListener::EventType::Unmounted, fileName,
        fileName);
    }
    else if(e->mask & IN_CLOSE)
    {
    }
    else
    { // ToDo: move directories, add to watch
      LOG_F(
        4, "directory '%s' inotify event %08X", fileName.c_str(), e->mask);
    }
  }
  else
  {
    std::stringstream ssLog;

    ssLog << "file '" << fileName << "' ";
    const bool movePending = m_cookie > 0 &&
      (std::chrono::steady_clock::now() - m_moveStart >
        FileSystemWatcherLinux::MoveTimeout);

    if(movePending)
    {
      // move to/from outer directory with only 1
      // notification
      m_cookie = 0;
      std::string n;
      m_dst.onFileSystemChange(
        IFileSystemChangeListener::EventType::FileMoved,
        m_moveFrom ? m_moveName : n, m_moveFrom ? n : m_moveName);
      ssLog << "movePending";
    }

    if(e->mask & IN_CLOSE_WRITE)
    {
      ssLog << "created";
      // file created
      m_dst.onFileSystemChange(
        IFileSystemChangeListener::EventType::FileCreated, "", fileName);
    }
    else if(e->mask & IN_DELETE)
    {
      ssLog << "deleted";
      // file created
      m_dst.onFileSystemChange(
        IFileSystemChangeListener::EventType::FileDeleted, "", fileName);
    }
    else if(e->mask & IN_MOVE && e->cookie)
    {
      if(m_cookie == 0)
      {
        ssLog << "move start (" << e->cookie << ")";
        // 1st part of possible 2 messages
        m_cookie = e->cookie;
        m_moveName = fileName;
        m_moveFrom = e->mask & IN_MOVED_FROM;
        m_moveStart = std::chrono::steady_clock::now();
      }
      else if(e->cookie == m_cookie)
      {
        ssLog << "move end (" << e->cookie << ")";
        // pair of move found
        m_cookie = 0;
        const bool from = e->mask & IN_MOVED_FROM;
        const std::string src = from ? fileName : m_moveName;
        const std::string dst = from ? m_moveName : fileName;

        m_dst.onFileSystemChange(
          IFileSystemChangeListener::EventType::FileMoved, src, dst);
      }
      else
      {
        LOG_F(ERROR, "INVALID INOTIFY EVENT: %s", fileName.c_str());
      }
    }
    else if(e->mask & IN_CLOSE_NOWRITE)
    {
      ssLog << "existing file closed: " << e->mask;
    }
    else
    {
      ssLog << "unknown: " << e->mask;
    }
    LOG_F(4, ssLog.str().c_str());
  }
}

void FileSystemWatcherLinux::threadMain()
{
  while(!m_stopping)
  {
    fd_set rfds;

    /* Watch to see when it has input. */
    FD_ZERO(&rfds);
    FD_SET(m_eventfd, &rfds);
    FD_SET(m_fd, &rfds);

    const int retval = select(
      std::max(m_fd, m_eventfd) + 1, &rfds, NULL, NULL, NULL);

    if(retval == -1)
    {
      LOG_F(ERROR, "select returned -1");
    }
    else if(retval)
    {
      if(FD_ISSET(m_eventfd, &rfds))
      {
        // internal request
        continue;
      }
      else if(FD_ISSET(m_fd, &rfds))
      {
        int u = 0;
        char buffer[1024];
        const ssize_t len = read(m_fd, buffer, sizeof(buffer));

        while(u < len)
        {
          const struct inotify_event *e = (struct inotify_event *)&buffer[u];
          handleInotifyEvent(e);

          u += sizeof(struct inotify_event) + e->len;
        }
      }
    }
    else
    {
      LOG_F(ERROR, "select returned 0");
    }
  }
}
}
