#ifndef __MEDIAARCHIVERDAEMONCONFIG_HPP__
#define __MEDIAARCHIVERDAEMONCONFIG_HPP__

#include <string>
#include <regex>

namespace MediaArchiver
{
struct DaemonConfig
{
  int verbosity;
  int serverPort;
  int aBitRate;
  int crf;
  int chunkSize;
  std::string foldersToWatch;
  std::regex filenameMatchPattern;
  std::string vCodec;
  std::string aCodec;
  std::string tempFolder;
  std::string finalExtension;
  std::string dbPath;
  std::string resultFileSuffix;
  std::string logFile;
};
}

#endif