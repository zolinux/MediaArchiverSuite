#define CATCH_CONFIG_MAIN
#include <catch.hpp>
#include <experimental/filesystem>
#include <fstream>

#include "FileUtils.hpp"
#include "MediaArchiverConfig.hpp"
#include "MediaArchiverClient.hpp"
#include "ServerMock.hpp"

#include "loguru.hpp"

using namespace MediaArchiver;
using namespace std;
using namespace std::experimental::filesystem;

const vector<string> testFiles = {"../test.mp4"};

namespace MediaArchiver
{
IServer *createServer(const ClientConfig &config)
{
  return new ServerMock(config, testFiles);
}

namespace
{
std::vector<std::string>::const_iterator g_currentFile = testFiles.cbegin();
bool mayExit = false;
}

bool ServerMock::getNextFile(const MediaFileRequirements &filter,
  MediaEncoderSettings &settings)
{
  if(g_currentFile == m_files.cend())
  {
    settings.fileLength = 0;
    mayExit = true;
    return false;
  }

  const auto fs = file_size(*g_currentFile);
  REQUIRE(fs > 0);

  settings.fileLength = fs;
  settings.fileExtension = "mp4";

  m_stream.open(*g_currentFile, ios::in | ios::binary);
  REQUIRE(m_stream.good());
  m_fileSize = fs;
  m_rxPos = 0U;
  LOG_F(INFO, "File %s opened for read (size=%lu)", g_currentFile->c_str(),
    fs);
  return true;
}

bool ServerMock::readChunk(std::ostream &file)
{
  REQUIRE(m_stream.is_open());
  REQUIRE(m_stream.good());
  REQUIRE(!m_stream.eof());
  REQUIRE(g_currentFile != m_files.cend());

  const size_t bytesToRead = m_fileSize - m_rxPos > m_config.chunkSize ?
    m_config.chunkSize :
    m_fileSize - m_rxPos;
  vector<char> buf(bytesToRead);
  LOG_F(9, "Chunk read. %lu bytes, pos=%lu", bytesToRead, m_rxPos);
  m_stream.read(buf.data(), buf.size());
  REQUIRE(m_stream.good());
  file.write(buf.data(), bytesToRead);
  m_rxPos += bytesToRead;
  return m_rxPos < m_fileSize && m_stream.good();
}

void ServerMock::postFile(const EncodingResultInfo &result)
{
  LOG_F(INFO, "File %s closed", g_currentFile->c_str());
  ++g_currentFile;
  m_fileSize = 0U;
}
}

class TestClient : public MediaArchiverClient
{
public:
  TestClient(const ClientConfig &cfg)
    : MediaArchiverClient(cfg)
  {
    m_encSettings = MediaEncoderSettings{
      .fileLength = 0,
      .encoderType = "ffmpeg",
      .fileExtension = "",
      .finalExtension = ".mp4",
      .commandLineParameters =
        "-y -t 1 -g 300 -c:v libaom-av1 -b:v 0 -movflags use_metadata_tags -crf 30 -c:a aac -b:a 80000",
    };
  }
};

TEST_CASE("ffmpeg_offline [pass]", "[ffmpegOFF]")
{
  int argc = 1;
  char fName[] = {__FILE__};
  char f2[] = {"0"};

  char *argv[] = {&fName[0], &f2[0]};
  loguru::init(argc, argv);
  loguru::add_file("test.log", loguru::FileMode::Append,
    static_cast<loguru::Verbosity>(9));
  loguru::g_internal_verbosity = 9;

  ClientConfig cfg;
  auto mac = MediaArchiverConfig<ClientConfig>(cfg);
  REQUIRE_NOTHROW(mac.read("../MediaArchiver.cfg"));
  REQUIRE_FALSE(cfg.pathToEncoder.empty());
  REQUIRE_FALSE(cfg.pathToProbe.empty());

  TestClient tc(cfg);
  while(tc.poll() == 0)
  {
    if(mayExit)
      break;
  }

  REQUIRE(true);
}
