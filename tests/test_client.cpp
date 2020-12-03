#define CATCH_CONFIG_MAIN
#include <catch.hpp>

#include "FileUtils.hpp"
#include "MediaArchiverConfig.hpp"
#include "MediaArchiverClient.hpp"

using namespace MediaArchiver;
using namespace std;

class TestClient : public MediaArchiverClient
{
public:
  TestClient(const ClientConfig &cfg)
    : MediaArchiverClient(cfg)
  {
    m_encSettings = MediaEncoderSettings{
      .fileLength = 1000,
      .encoderType = "ffmpeg",
      .fileExtension = "",
      .finalExtension = ".mp4",
      .commandLineParameters =
        "-preset veryfast -c:v libx265 -c:a aac -crf 21 -b:a 80000",
    };
  }

  void testFfprobe(const string &path)
  {
    int len = 0;
    REQUIRE_NOTHROW(len = getMovieLength(path));
    REQUIRE(len == 30);
  }

  void testEncoding(const string &path)
  {
    stringstream ss;

    size_t posExt = path.rfind('.');
    string outFile = path.substr(0, posExt) + "_archvd" +
      m_encSettings.finalExtension;

    ss << m_cfg.pathToEncoder << " -y -hide_banner -i \"" << path << "\" "
       << m_encSettings.commandLineParameters << " \"" << outFile
       << "\" 2>&1";

    string output;
    int retcode;
    REQUIRE_NOTHROW(launch(ss.str()));
    REQUIRE_NOTHROW(retcode = waitForFinish(output));
    cout << "Encoder output " << retcode << ": " << output << endl;
    REQUIRE_FALSE(retcode);

    int outFileLen = 0;
    REQUIRE_NOTHROW(outFileLen = getMovieLength(outFile));
    REQUIRE(outFileLen > 0);

    int inFileLen = 0;
    REQUIRE_NOTHROW(inFileLen = getMovieLength(path));
    REQUIRE(outFileLen == inFileLen);
  }

private:
};

TEST_CASE("ffmpeg [pass]", "[ffmpeg]")
{
  ClientConfig cfg;
  auto mac = MediaArchiverConfig<ClientConfig>(cfg);
  REQUIRE_NOTHROW(mac.read("../MediaArchiver.cfg"));
  REQUIRE_FALSE(cfg.pathToEncoder.empty());
  REQUIRE_FALSE(cfg.pathToProbe.empty());

  TestClient tc(cfg);
  const string srcName("../../test.avi");

  SECTION("FFPROBE") { tc.testFfprobe(srcName); }
  SECTION("ENCODING") { tc.testEncoding(srcName); }
  REQUIRE(true);
}