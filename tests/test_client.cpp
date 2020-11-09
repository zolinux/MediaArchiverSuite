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
  }

  void testFfprobe(const string &path)
  {
    int len = 0;
    REQUIRE_NOTHROW(len = getMovieLength(path));
    REQUIRE(len == 7 * 60 + 35);
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
  tc.testFfprobe(
    "C:\\Users\\zoltan.dezsi\\Pictures\\2020\\_all\\20200102_120937.mp4");
}