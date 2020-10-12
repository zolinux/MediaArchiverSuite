#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include "FileSystemWatcher.hpp"

#define CATCH_CONFIG_MAIN
#include <catch.hpp>

using namespace MediaArchiver;
using namespace std;

class EventListener : public IFileSystemChangeListener
{
private:
  /* data */
public:
  EventListener(/* args */) {}
  ~EventListener() {}
  EventType evt;
  string src;
  string dst;
  void onFileSystemChange(
    EventType e, const string &src, const string &dst) override
  {
    cout << "onFileSystemChange Type: " << static_cast<int>(e)
         << " File: " << dst << endl;
    evt = e;
    this->src = src;
    this->dst = dst;
  }
};

const string folder = "/tmp/test_inotify";
list<string> g_folders{{folder}};

TEST_CASE("linux file system watcher (pass)", "[watcher]")
{
  system((string("rm -rf ") + folder).c_str());
  auto ret = mkdir(folder.c_str(), S_IRWXU);
  REQUIRE(ret >= 0);
  string sf1 = folder + "/1";
  string sf2 = folder + "/2";
  string sf12 = sf1 + "/3";
  usleep(10000);
  mkdir(sf1.c_str(), S_IRWXU);
  usleep(10000);
  mkdir(sf12.c_str(), S_IRWXU);
  usleep(10000);
  mkdir(sf2.c_str(), S_IRWXU);
  string f1 = sf12 + "/f1.txt";
  system((string("touch ") + sf1 + "/existingFile.dat").c_str());

  EventListener e;
  unique_ptr<IFileSystemWatcher> fsw(
    FileSystemWatcher::create(e, g_folders));

  usleep(10000);
  system((string("touch ") + f1).c_str());

  getchar();
  REQUIRE(e.evt == EventListener::EventType::FileCreated);
  REQUIRE(e.dst == f1);
}
