
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
  std::chrono::milliseconds(250);

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
  int fd = inotify_init1(IN_NONBLOCK);
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
      auto argument = folder.substr(DirWatchRegexPrefix.length());
      auto separator = argument.find('*');
      if(separator == std::string::npos)
      {
        throw std::runtime_error(
          std::string("regex format invalid: ") + argument);
      }

      auto startFolder = argument.substr(0, separator);
      auto re = argument.substr(separator + 1);

      std::stringstream ss;
      ss << "/usr/bin/find " << startFolder << " -type d -iregex '" << re
         << "'";
      std::string command = ss.str();
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
        while((getline(&line, &len, f.get())) != -1)
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
    std::string name;
  };

  std::vector<dir_t> stack;
  stack.emplace_back(dir_t{nullptr, dirName});

  while(stack.size())
  {
    struct dirent *entry;
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
        auto wd = inotify_add_watch(
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
      auto fullName = cd.name + "/" + entry->d_name;
      if(entry->d_type == DT_REG)
      {
        m_dst.onFileSystemChange(
          IFileSystemChangeListener::EventType::FileDiscovered, "",
          fullName);
      }
      else if(entry->d_type == DT_DIR)
      {
        std::string name(entry->d_name);
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
      std::cerr << "directory " << fileName << " was deleted" << std::endl;
      inotify_rm_watch(m_fd, e->wd);
      m_watchedDirs.erase(e->wd);
    }
    else if(e->mask & IN_CREATE)
    {
      // directory created. add it to watched list
      auto ret = inotify_add_watch(
        m_fd, fileName.c_str(), FileSystemWatcherLinux::InotifyFlags);
      if(ret < 0)
      {
        std::cerr << "directory " << fileName
                  << " could not be added to watched directories:" << ret
                  << std::endl;
      }
      else
      {
        m_watchedDirs[ret] = fileName;
        std::cerr << "directory " << fileName << " was created"
                  << std::endl;
      }
    }
    else if(e->mask & IN_UNMOUNT)
    {
      // directory unmounted
      std::cerr << "directory " << fileName << " was unmounted"
                << std::endl;
      m_dst.onFileSystemChange(
        IFileSystemChangeListener::EventType::Unmounted, fileName,
        fileName);
    }
    else if(e->mask & IN_CLOSE)
    {
    }
    else
    { // ToDo: move directories, add to watch
      std::cerr << "directory " << fileName << " was " << e->mask
                << std::endl;
    }
  }
  else
  {
    std::cerr << "file " << fileName << " ";
    bool movePending = m_cookie > 0 &&
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
      std::cerr << "movePending ";
    }

    if(e->mask & IN_CLOSE_WRITE)
    {
      std::cerr << "created";
      // file created
      m_dst.onFileSystemChange(
        IFileSystemChangeListener::EventType::FileCreated, "", fileName);
    }
    else if(e->mask & IN_DELETE)
    {
      std::cerr << "deleted ";
      // file created
      m_dst.onFileSystemChange(
        IFileSystemChangeListener::EventType::FileDeleted, "", fileName);
    }
    else if(e->mask & IN_MOVE)
    {
      if(e->cookie != m_cookie || e->cookie == 0)
      {
        std::cerr << "move start ";
        // 1st part of possible 2 messages
        m_cookie = e->cookie;
        m_moveName = fileName;
        m_moveFrom = e->mask & IN_MOVED_FROM;
        m_moveStart = std::chrono::steady_clock::now();
      }
      else if(e->cookie == m_cookie)
      {
        std::cerr << "move end ";
        // pair of move found
        m_cookie = 0;
        bool from = e->mask & IN_MOVED_FROM;
        std::string src = from ? fileName : m_moveName;
        std::string dst = from ? m_moveName : fileName;

        m_dst.onFileSystemChange(
          IFileSystemChangeListener::EventType::FileMoved, src, dst);
      }
    }
    else if(e->mask & IN_CLOSE_NOWRITE)
    {
      std::cerr << "existing file closed: " << e->mask;
    }
    else
    {
      std::cerr << "unknown: " << e->mask;
    }
    std::cerr << std::endl;
  }
}

void FileSystemWatcherLinux::threadMain()
{
  while(!m_stopping)
  {
    fd_set rfds;
    int retval;

    /* Watch to see when it has input. */
    FD_ZERO(&rfds);
    FD_SET(m_eventfd, &rfds);
    FD_SET(m_fd, &rfds);

    retval = select(std::max(m_fd, m_eventfd) + 1, &rfds, NULL, NULL, NULL);

    if(retval == -1)
    {
      std::cerr << "select returned -1" << std::endl;
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
        ssize_t len = read(m_fd, buffer, sizeof(buffer));

        while(u < len)
        {
          struct inotify_event *e = (struct inotify_event *)&buffer[u];
          handleInotifyEvent(e);

          u += sizeof(struct inotify_event) + e->len;
        }
      }
    }
    else
    {
      std::cerr << "select returned 0" << std::endl;
    }
  }
}
}
