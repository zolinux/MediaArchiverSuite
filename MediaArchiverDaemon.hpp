#ifndef __MEDIAARCHIVERDAEMON_HPP__
#define __MEDIAARCHIVERDAEMON_HPP__

#include <utility>
#include <string>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <future>
#include <sstream>
#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>
#include <deque>
#include <condition_variable>

#include "IMediaArchiverServer.hpp"
#include "MediaArchiverDaemonConfig.hpp"
#include "IFileSystemChangeListener.hpp"
#include "IDatabase.hpp"
#include "rpc/server.h"

namespace MediaArchiver
{
struct ConnectedClient
{
  MediaFileRequirements filter;
  MediaEncoderSettings encSettings;
  EncodingResultInfo encResult;
  uint32_t originalFileId;
  std::string originalFileName;
  std::string tempFileName;
  std::chrono::steady_clock::time_point lastActivify;
  std::string token;
  std::ifstream inFile;
  std::ofstream outFile;
  struct timespec times[2];
};

struct FileToMove
{
  const EncodedFile result;
  const std::string tmp;
  struct timespec atime;
  struct timespec mtime;
};

class MediaArchiverDaemon : public IFileSystemChangeListener
{
private:
  std::atomic<bool> m_shutdown;
  std::atomic<bool> m_stopRequested;
  const DaemonConfig &m_cfg;
  IDatabase &m_db;
  rpc::server m_srv;
  std::map<rpc::session_id_t, ConnectedClient> m_connections;
  std::mutex m_mtxFileMove;
  std::deque<FileToMove> m_filesToMove;
  std::condition_variable m_cv;

public:
  MediaArchiverDaemon(const DaemonConfig &cfg, IDatabase &db);
  MediaArchiverDaemon(const MediaArchiverDaemon &) = delete;
  MediaArchiverDaemon(MediaArchiverDaemon &&) = default;
  ~MediaArchiverDaemon();

  void init();
  void start();
  void stop(bool forced);
  bool isStopRequested() const { return m_stopRequested; }
  void onFileSystemChange(IFileSystemChangeListener::EventType e,
    const std::string &src, const std::string &dst) override;

protected:
  uint32_t getVersion() const;
  bool isConnected() const;
  bool isIdle();
  void authenticate(const std::string &token);
  void reset();
  void abort();
  bool getNextFile(ConnectedClient &cli,
    const MediaFileRequirements &filter, MediaEncoderSettings &settings);
  bool readChunk(DataChunk &chunk);
  void postFile(const EncodingResultInfo &result);
  bool writeChunk(const std::vector<char> &data);
  std::string getArchivedFileName(const std::string &origFileName) const;
  bool isArchive(const std::string &fileName) const;
  bool isInterestingFile(const std::string &fileName) const;
  /**
   * @brief put current result file into queue and
   * clear current session
   */
  void prepareNewSession(ConnectedClient &cli);
  ConnectedClient &checkClient();
};

}
#endif // !__MEDIAARCHIVERDAEMON_HPP__