#include "MediaArchiverClientConfig.hpp"
#include "MediaArchiverClient.hpp"

#include "ServerIf.hpp"

namespace MediaArchiver
{
IServer *createServer(const ClientConfig &config)
{
  return new ServerIf(config);
}
}
