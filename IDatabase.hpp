#ifndef __IDATABASE_HPP__
#define __IDATABASE_HPP__

#include <exception>
#include <string>

#include "IMediaArchiverServer.hpp"

namespace MediaArchiver
{
struct BasicFileInfo
{
  std::string fileName;
  size_t fileSize;
};

struct EncodedFile : public EncodingResultInfo
{
  uint32_t originalFileId;
  std::string fileName;

  EncodedFile(){};

  EncodedFile(const EncodingResultInfo &resInfo, uint32_t origId)
    : EncodingResultInfo(resInfo)
    , originalFileId(origId)
  {
  }

  EncodedFile(const EncodingResultInfo &resInfo, uint32_t origId,
    const std::string &fName)
    : EncodingResultInfo(resInfo)
    , originalFileId(origId)
    , fileName(fName)
  {
  }
};

class IDatabase
{
public:
  virtual void init() = 0;
  /**
   * @brief Connect to DB
   *
   * @param connectionString
   * @param create creates the tables if not present instead of throwing
   * error
   */
  virtual void connect(const char *connectionString, bool create) = 0;
  virtual void disconnect() = 0;
  /**
   * Reserve the next media file for processing.
   *
   * @param filter filter by client
   * @param file basic file info
   * @return uint32_t file ID in source table or 0 if no file found
   */
  virtual uint32_t getNextFile(
    const MediaFileRequirements &filter, BasicFileInfo &file) = 0;
  /**
   * Adds a media file to original source media table (if not exists) and
   * returns its id
   *
   * @param src original media file found or nullptr
   * @param dst encodec media file found or nullptr
   * @param queue should the new file be added to queue
   * @return uint32_t original file id if applicable or 0
   */
  virtual uint32_t addFile(
    const BasicFileInfo *src, const BasicFileInfo *dst, bool queue) = 0;

  /**
   * Use after encoding a file to add it into archives table
   *
   * @param file details of processed media file
   */
  virtual void addEncodedFile(const EncodedFile &file) = 0;

  virtual ~IDatabase(){};
};

class DBError : std::exception
{
public:
  enum class DBErrorCodes
  {
    None,
    ConnectionError,
    AccessDenied,
    EmptyDatabase,
    AlreadyInitialized,
    SqlError,
    InvalidDataReturned,
  };

  DBError(DBErrorCodes id, const std::string &message)
    : m_id(id)
    , m_msg(message)
  {
  }
  DBError(DBErrorCodes id, const char *message)
    : m_id(id)
    , m_msg(message)
  {
  }

  bool operator==(const DBErrorCodes id) const { return m_id == id; }

  virtual const char *what() const noexcept override
  {
    return m_msg.c_str();
  }

private:
  DBErrorCodes m_id;
  const std::string m_msg;
};
}
#endif