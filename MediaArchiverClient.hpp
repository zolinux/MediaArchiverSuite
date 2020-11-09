#ifndef __MEDIAARCHIVERCLIENT_HPP__
#define __MEDIAARCHIVERCLIENT_HPP__

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

#include "IMediaArchiverServer.hpp"
#include "MediaArchiverClientConfig.hpp"

namespace MediaArchiver
{
class MediaArchiverClient
{
protected:
  static constexpr const char *InTmpFileName = "infile00";
  static constexpr const char *OutTmpFileName = "outfile00";
  std::unique_ptr<FILE, decltype(&pclose)> m_encodeProcess;
  std::unique_ptr<MediaArchiver::IServer> m_rpc;
  std::stringstream m_stdOut;
  std::atomic<bool> m_shutdown;
  std::atomic<bool> m_stopRequested;
  std::ofstream m_srcFile;
  std::ifstream m_dstFile;
  std::chrono::steady_clock::time_point m_startTime;
  int m_token;
  const ClientConfig &m_cfg;
  MediaFileRequirements m_filter;
  MediaEncoderSettings m_encSettings;
  EncodingResultInfo m_encResult;
  int m_timeToWait;
  bool m_authenticated;

  enum class MainStates
  {
    Idle,
    WaitForReconnect,
    WaitForConnect,
    WaitForFileCheck,
    Authenticateing,
    Receiving,
    WaitForEncodingFinished,
    SendResult,
    Transmitting,
  };

  MainStates m_mainState = MainStates::Idle;
  MainStates m_prevMainState = MainStates::Idle;

  void doIdle();
  void doAuth();
  void doWait();
  void doTransmit();
  void doReceive();
  void doConvert();
  void doSendResult();

  void launch(const std::string &cmdLine);
  int waitForFinish(std::string &stdOut);
  int getMovieLength(const std::string &path);
  void removeTempFiles();
  void cleanUp();
  void checkCreateRpc();

public:
  MediaArchiverClient(const ClientConfig &cfg);
  MediaArchiverClient(const MediaArchiverClient &) = delete;
  MediaArchiverClient(MediaArchiverClient &&) = default;
  ~MediaArchiverClient();

  void init();
  void stop(bool forced);
  bool isStopRequested() const { return m_stopRequested; }
  int poll();
};

}
#endif // !__MEDIAARCHIVERCLIENT_HPP__