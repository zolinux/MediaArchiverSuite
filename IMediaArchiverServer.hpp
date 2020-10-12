#ifndef __IMEDIAARCHIVERSERVER_HPP__
#define __IMEDIAARCHIVERSERVER_HPP__

#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>

#ifndef NORPC
  #include "rpc/msgpack.hpp"
  #define MSGPACK_DEFINE_ARRAY_(...) MSGPACK_DEFINE_ARRAY(__VA_ARGS__)
#else
  #define MSGPACK_DEFINE_ARRAY_(...)
#endif // !NORPC

namespace MediaArchiver
{
class IOError : public std::runtime_error
{
public:
  IOError(const std::string &what)
    : std::runtime_error(what){};
};

class NetworkError : public std::runtime_error
{
public:
  NetworkError(const std::string &what)
    : std::runtime_error(what){};
};

class IVersion
{
public:
  virtual uint32_t getVersion() const = 0;
  virtual ~IVersion(){};
};

struct MediaFileRequirements
{
  std::string encoderType;
  size_t maxFileSize;
  MSGPACK_DEFINE_ARRAY_(encoderType, maxFileSize)
};

struct MediaEncoderSettings
{
  size_t fileLength;
  std::string encoderType;
  std::string fileExtension;
  std::string commandLineParameters;
  MSGPACK_DEFINE_ARRAY_(
    fileLength, encoderType, fileExtension, commandLineParameters)
};

struct EncodingResultInfo
{
  enum EncodingResult : int8_t
  {
    Started = 1,
    OK = 5,
    NotStarted = 0,
    RetriableError = -1,
    ServerIOError = -9,
    UnknownError = -50,
    PermanentError = -100,
  };

  int8_t result;
  size_t fileLength;
  std::string error;
  MSGPACK_DEFINE_ARRAY_(result, fileLength, error)
  EncodingResultInfo() {}
  EncodingResultInfo(EncodingResult result, size_t size, std::string error)
    : result(static_cast<int8_t>(result))
    , fileLength(size)
    , error(error)
  {
  }
};

using DataChunk = std::vector<char>;
class IServer : public IVersion
{
public:
  virtual bool isConnected() const = 0;

  virtual void authenticate(const std::string &token) = 0;
  virtual void reset() = 0;
  virtual bool getNextFile(const MediaFileRequirements &filter,
    MediaEncoderSettings &settings) = 0;
  virtual bool readChunk(std::ostream &file) = 0;
  virtual void postFile(const EncodingResultInfo &result) = 0;
  virtual bool writeChunk(const std::vector<char> &data) = 0;
  virtual ~IServer(){};
};
}
#endif // !__IMEDIAARCHIVERSERVER_HPP__