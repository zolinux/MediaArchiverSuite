#include "SQLite.hpp"

#define CATCH_CONFIG_MAIN
#include <catch.hpp>

using namespace MediaArchiver;

TEST_CASE("escaping text (pass)", "[escape]")
{
  const std::string sRaw = "this 'is' string";
  const std::string sEsc = "this ''is'' string";

  REQUIRE(ExecSQL::escape(sRaw) == sEsc);

  ExecSQL sql(nullptr);
  sql << (int)80000 << (size_t)120000;
}

TEST_CASE("open db (pass)", "[sqlite]")
{
  BasicFileInfo src42;
  BasicFileInfo dst;

  SQLite db;
  db.init();
  db.connect("/tmp/test.db", true);

  auto src10000 = BasicFileInfo{.fileName = "ss", .fileSize = 10000};
  auto id1 = db.addFile(&src10000, nullptr, false);
  REQUIRE(id1 == 1);

  src42 = {.fileName = "ss/42", .fileSize = 42};
  auto id2 = db.addFile(&src42, nullptr, false);
  REQUIRE(id2 == 2);

  auto src1000 = BasicFileInfo{.fileName = "ss/1000", .fileSize = 1000};
  auto id3 = db.addFile(&src1000, nullptr, false);
  REQUIRE(id3 == 3);

  auto mfr = MediaFileRequirements{.encoderType = "ffmpeg",
    .maxFileSize = 5000};
  auto id4 = db.getNextFile(mfr, dst);
  REQUIRE(id4 == 2);

  auto srcf100 = BasicFileInfo{.fileName = "/mnt/home/pi/MAS/ss.mov", .fileSize = 1000};
  auto dstf100 = BasicFileInfo{.fileName = "/mnt/home/pi/MAS/ss_archived.mp4", .fileSize = 200};
  auto id6 = db.addFile(&srcf100, &dstf100, false);
  REQUIRE(id6 == 4);

  auto id5 = db.getNextFile(mfr, dst);
  REQUIRE(id5 == 3);

  EncodedFile ef4(
    {
      EncodingResultInfo::EncodingResult::OK,
      3000,
      "",
    },
    id4);

  db.addEncodedFile(ef4);

  EncodedFile ef5(
    {
      EncodingResultInfo::EncodingResult::PermanentError,
      100,
      "Test",
    },
    id5);

  db.addEncodedFile(ef5);

  db.disconnect();
}

