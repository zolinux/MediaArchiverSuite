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
#include "FileUtils.hpp"

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
  mt.seed(time(nullptr));
  std::uniform_int_distribution<> dist(0, std::numeric_limits<int>::max());
  snprintf(buf, sizeof(buf), "%X", dist(mt));
  gToken = std::string(buf);
}

std::unique_ptr<ServerIf> connect()
{
  auto rpc = std::make_unique<ServerIf>(gCfg);
  REQUIRE(rpc);

  uint32_t ver = 0;
  REQUIRE_NOTHROW(ver = rpc->getVersion());
  REQUIRE(ver);

  if(gToken.empty())
    generateToken();
  REQUIRE_NOTHROW(rpc->authenticate(gToken));
  return rpc;
}

void getNextFile(std::unique_ptr<ServerIf> &rpc, MediaEncoderSettings &mes)
{
  const MediaFileRequirements mfrq{.encoderType = "ffmpeg",
    .maxFileSize = 100u * 1024 * 1024};

  bool success = false;
  REQUIRE_NOTHROW(success = rpc->getNextFile(mfrq, mes));
  REQUIRE(success);
  REQUIRE(mes.fileLength > 0);
  REQUIRE(!mes.fileExtension.empty());
  REQUIRE(!mes.finalExtension.empty());
  REQUIRE(!mes.commandLineParameters.empty());
}

TEST_CASE("rpc service (pass)", "[rpc]")
{
  MediaEncoderSettings mes;
  bool success = false;

  auto rpc = connect();
  getNextFile(rpc, mes);

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

  getNextFile(rpc, mes);

  const string fName("file.tmp");
  ofstream writeFile(fName, ios::binary);
  do {
    REQUIRE_NOTHROW(success = rpc->readChunk(writeFile));
    const auto pos = writeFile.tellp();
    REQUIRE(pos >= 0);
    REQUIRE(mes.fileLength >= pos);
  } while(success);

  REQUIRE(writeFile.tellp() == mes.fileLength);
  writeFile.close();

  // simulate encoded file by writing the first ~1MB
  size_t fSize = 1u * 1024 * 1024 + 42;
  EncodingResultInfo eri(EncodingResultInfo::EncodingResult::OK, fSize, "");
  ifstream readFile(fName, ios::binary);

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

TEST_CASE("file transfer antitest (pass)", "[encfail]")
{
  MediaEncoderSettings mes;
  bool success = false;

  auto rpc = connect();
  getNextFile(rpc, mes);

  DataChunk file;
  size_t length = 0;

  do
  {
    REQUIRE_NOTHROW(success = rpc->readChunk(file));
    length += file.size();
  } while(success);

  REQUIRE(length == mes.fileLength);

  EncodingResultInfo eri(EncodingResultInfo::EncodingResult::PermanentError,
    0, "Very bad fatal error");
  REQUIRE_NOTHROW(rpc->postFile(eri));

  getNextFile(rpc, mes);
  REQUIRE_NOTHROW(rpc->abort());
}

TEST_CASE("connection error during transfer (pass)", "[networkerror]")
{
  MediaEncoderSettings mes;
  bool success = false;

  auto rpc = connect();
  getNextFile(rpc, mes);

  DataChunk file;

  const int disconnectAtChunk = 3;
  int chunks = 0;
  do
  {
    REQUIRE_NOTHROW(success = rpc->readChunk(file));
  } while(chunks++ < disconnectAtChunk);

  // let the connection really break;
  rpc.reset();
  std::this_thread::sleep_for(std::chrono::seconds(4));
  size_t length = 0;

  // reconnect
  rpc = connect();
  rpc->reset();
  do
  {
    REQUIRE_NOTHROW(success = rpc->readChunk(file));
    length += file.size();
  } while(success);

  REQUIRE(length == mes.fileLength);

  length >>= 1;
  EncodingResultInfo eri(
    EncodingResultInfo::EncodingResult::OK, length, "");
  REQUIRE_NOTHROW(rpc->postFile(eri));

  file.resize(gCfg.chunkSize);
  chunks = 0;
  do
  {
    REQUIRE_NOTHROW(success = rpc->writeChunk(file));
  } while(chunks++ < disconnectAtChunk);

  // let the connection really break;
  REQUIRE_NOTHROW(rpc.reset());
  std::this_thread::sleep_for(std::chrono::seconds(4));

  REQUIRE_NOTHROW(rpc = connect());
  REQUIRE_NOTHROW(rpc->reset());

  chunks = 0;
  while(length)
  {
    if(length < gCfg.chunkSize)
    {
      file.resize(length);
    }

    REQUIRE_NOTHROW(success = rpc->writeChunk(file));
    length -= file.size();
    REQUIRE((length == 0) ^ success);
  }
}

TEST_CASE("setfileTime", "[filetime]")
{
  timespec ts[2];
  timespec ts2[2];

  FileUtils().getFileTimes("/etc/passwd", ts);
  const char *fname = "/tmp/test.txt";
  system((std::string("touch ") + fname).c_str());
  FileUtils().setFileTimes(fname, ts);
  memset(ts2, 0, sizeof(ts2));
  FileUtils().getFileTimes(fname, ts2);
  REQUIRE(ts2[0].tv_sec == ts[0].tv_sec);
  REQUIRE(ts2[0].tv_nsec == ts[0].tv_nsec);
  REQUIRE(ts2[1].tv_sec == ts[1].tv_sec);
  REQUIRE(ts2[1].tv_nsec == ts[1].tv_nsec);
}