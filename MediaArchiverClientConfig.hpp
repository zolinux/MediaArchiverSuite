#ifndef __MEDIAARCHIVERCLIENTCONFIG_HPP__
#define __MEDIAARCHIVERCLIENTCONFIG_HPP__

#include <string>

namespace MediaArchiver
{
struct ClientConfig
{
  int serverConnectionTimeout;
  int verbosity;
  int checkForNewFileInterval;
  int reconnectDelay;
  int serverPort;
  size_t chunkSize;
  std::string serverName;
  std::string pathToEncoder;
  std::string pathToProbe;
  std::string tempFolder;
  std::string extraCommandLineOptions;
};
}

#endif