#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <memory>
#include <sstream>
#include <fcntl.h>
#include <streambuf>

#define CATCH_CONFIG_MAIN
#include <catch.hpp>

#include "ServerIf.hpp"

using namespace MediaArchiver;
using namespace std;

static ClientConfig gCfg{
  .serverConnectionTimeout = 5000,
  .verbosity = 0,
  .checkForNewFileInterval = 60000,
  .reconnectDelay = 3333,
  .serverPort = 2020,
  .chunkSize = 256 * 1024,
  .serverName = "localhost",
  .pathToEncoder = "",
  .pathToProbe = "",
#ifdef WIN32
  .tempFolder = ".",
#else
  .tempFolder = "/tmp",
#endif
};

static std::string gToken;

void generateToken()
{
  char buf[64];
  std::random_device rd;
  std::mt19937 mt(rd());
  std::uniform_int_distribution<> dist(0, std::numeric_limits<int>::max());
  snprintf(buf, sizeof(buf), "%X", dist(mt));
  gToken = std::string(buf);
}

TEST_CASE("rpc service (pass)", "[rpc]")
{
  auto rpc = std::make_unique<ServerIf>(gCfg);
  REQUIRE(rpc);

  uint32_t ver = 0;
  REQUIRE_NOTHROW(ver = rpc->getVersion());
  REQUIRE(ver);

  generateToken();
  REQUIRE_NOTHROW(rpc->authenticate(gToken));

  const MediaFileRequirements mfrq{.encoderType = "ffmpeg",
    .maxFileSize = 100u * 1024 * 1024};

  MediaEncoderSettings mes;
  bool success = false;
  REQUIRE_NOTHROW(success = rpc->getNextFile(mfrq, mes));
  REQUIRE(success);
  REQUIRE(mes.fileLength > 0);
  REQUIRE(!mes.fileExtension.empty());
  REQUIRE(!mes.commandLineParameters.empty());

  DataChunk file[2];
  int idx = 0;

  while(idx < 2)
  {
    REQUIRE_NOTHROW(success = rpc->readChunk(file[idx]));
    REQUIRE(success);
    idx++;
  }

  REQUIRE(file[1].size());
  REQUIRE(file[0].size() == file[1].size());

  // should differ due to sequential read, later it will match
  REQUIRE(memcmp(file[0].data(), file[1].data(), file[0].size()));
  REQUIRE_NOTHROW(rpc->reset());
  REQUIRE_NOTHROW(success = rpc->readChunk(file[1]));
  REQUIRE(success);

  REQUIRE(file[1].size());
  REQUIRE(file[0].size() == file[1].size());
  REQUIRE_FALSE(memcmp(file[0].data(), file[1].data(), file[0].size()));

  REQUIRE_NOTHROW(rpc->abort());
  REQUIRE_THROWS(rpc->readChunk(file[0]));
  REQUIRE_THROWS(rpc->writeChunk(file[0]));
  REQUIRE_NOTHROW(rpc->reset());

  REQUIRE_NOTHROW(success = rpc->getNextFile(mfrq, mes));
  REQUIRE(success);
  REQUIRE(mes.fileLength > 0);
  REQUIRE(!mes.fileExtension.empty());
  REQUIRE(!mes.commandLineParameters.empty());

  const string fName("file.tmp");
  ofstream writeFile(fName);
  do
  {
    REQUIRE_NOTHROW(success = rpc->readChunk(writeFile));
  } while(success);

  REQUIRE(writeFile.tellp() == mes.fileLength);
  writeFile.close();

  // simulate encoded file by writing the first ~1MB
  size_t fSize = 1u * 1024 * 1024 + 42;
  EncodingResultInfo eri(EncodingResultInfo::EncodingResult::OK, fSize, "");
  ifstream readFile(fName);

  REQUIRE_NOTHROW(rpc->postFile(eri));
  while(fSize)
  {
    const auto len = fSize > gCfg.chunkSize ? gCfg.chunkSize : fSize;
    DataChunk chunk(len);

    readFile.readsome(chunk.data(), len);
    REQUIRE_NOTHROW(success = rpc->writeChunk(chunk));
    fSize -= len;
    REQUIRE(success == (fSize > 0));
  }

  readFile.close();
  std::remove(fName.c_str());
  REQUIRE_THROWS(rpc->writeChunk(file[0]));
}
