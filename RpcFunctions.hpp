#ifndef __RpcFunctions_hpp__
#define __RpcFunctions_hpp__

#include <string>

namespace MediaArchiver
{
namespace RpcFunctions
{
const char authenticate[] = "authenticate";
const char getVersion[] = "getVersion";
const char reset[] = "reset";
const char abort[] = "abort";
const char getNextFile[] = "getNextFile";
const char readChunk[] = "readChunk";
const char postFile[] = "postFile";
const char writeChunk[] = "writeChunk";
};
}
#endif