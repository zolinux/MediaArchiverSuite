#ifndef __FILESYSTEMWATCHER_HPP__
#define __FILESYSTEMWATCHER_HPP__

#if defined(WIN32)
#include "FileSystemWatcherWindows.hpp"
#define FSWClass FileSystemWatcherWindows
#else
#include "FileSystemWatcherLinux.hpp"
#define FSWClass FileSystemWatcherLinux
#endif

namespace MediaArchiver
{
    class FileSystemWatcher
    {
        public:
        static IFileSystemWatcher *create(IFileSystemChangeListener &listener, const std::list<std::string> &folders)
        {
            return new FSWClass(listener, folders);
        }
    };
}
#endif