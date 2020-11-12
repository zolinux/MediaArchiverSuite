#define CATCH_CONFIG_MAIN
#include <catch.hpp>

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
    REQUIRE(len == 7 * 60 + 35);
  }

  void testEncoding(const string &path)
  {
    int len = 0;
    stringstream ss;

    size_t posExt = path.rfind('.');
    string outFile = path.substr(0, posExt) + "_archvd" +
      m_encSettings.finalExtension;

    ss << m_cfg.pathToEncoder << " " << m_encSettings.commandLineParameters
       << " -i \"" << path << "\" -o \"" << m_cfg.tempFolder << "/"
       << OutTmpFileName << m_encSettings.finalExtension << "\" 2>&1";

    string output;
    int retcode;
    REQUIRE_NOTHROW(launch(ss.str()));
    REQUIRE_NOTHROW(retcode = waitForFinish(output));
    cout << "Encoder output " << retcode << ": " << output << endl;
    REQUIRE_FALSE(retcode);
  }

private:
};

TEST_CASE("ffprobe [pass]", "[ffprobe]")
{
  ClientConfig cfg;
  auto mac = MediaArchiverConfig<ClientConfig>(cfg);
  auto b = mac.read("../MediaArchiver.cfg");
  REQUIRE_FALSE(cfg.pathToEncoder.empty());
  REQUIRE_FALSE(cfg.pathToProbe.empty());

  TestClient tc(cfg);
  const string srcName(
#ifdef WIN32
    "C:\\Users\\zoltan.dezsi\\Pictures\\2020\\2020-01-Itthon\\20200102_120937.mp4"
#else
    "~/Pictures/2021/file_example_MOV_480_700kB.mov"
#endif // WIN32
  );

  SECTION("FFPROBE") { tc.testFfprobe(srcName); }

  SECTION("ENCODING") { tc.testEncoding(srcName); }
}