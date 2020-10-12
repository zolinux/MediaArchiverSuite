#ifndef __RpcFunctions_hpp__
#define __RpcFunctions_hpp__

#include <string>

namespace MediaArchiver
{
class RpcFunctions
{
public:
  static constexpr const char *authenticate = "authenticate";
  static constexpr const char *getVersion = "getVersion";
  static constexpr const char *reset = "reset";
  static constexpr const char *getNextFile = "getNextFile";
  static constexpr const char *readChunk = "readChunk";
  static constexpr const char *postFile = "postFile";
  static constexpr const char *writeChunk = "writeChunk";
};
}
#endif