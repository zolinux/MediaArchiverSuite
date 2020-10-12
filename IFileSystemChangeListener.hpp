/**
 * @file IFileSystemChangeListener.hpp
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2020-09-12
 *
 * @copyright Copyright (c) 2020
 *
 */

#ifndef __IFILESYSTEMCHANGELISTENER_HPP__
#define __IFILESYSTEMCHANGELISTENER_HPP__

#include <string>

class IFileSystemChangeListener
{
public:
  enum class EventType : int8_t
  {
    None = 0,
    FileCreated,
    FileDeleted,
    FileModified,
    FileMoved,
    FileDiscovered,
    Unmounted,
    Unknown = -1
  };

  /**
   * @brief function gets called on every file system notification captured
   * by the agent
   *
   * @param t
   * @param src
   * @param dst
   */
  virtual void onFileSystemChange(
    EventType e, const std::string &src, const std::string &dst) = 0;

  virtual ~IFileSystemChangeListener() = default;
};

#endif // !__IFileSystemChangeListener_HPP__