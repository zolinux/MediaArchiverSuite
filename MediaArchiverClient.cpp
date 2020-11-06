#include <utility>
#include <string>
#include <iostream>
#include <cstdio>
#include <algorithm>
#include <numeric>
#include <future>
#include <sstream>
#include <mutex>
#include <thread>
#include <chrono>
#include <signal.h>
#include <dirent.h>
#include <regex>
#include <random>

#include "rpc/client.h"
#include "rpc/rpc_error.h"
#include "MediaArchiverClient.hpp"
#include "MediaArchiverConfig.hpp"
#include "ServerIf.hpp"

static std::unique_ptr<MediaArchiver::MediaArchiverClient> gima;

static MediaArchiver::ClientConfig gCfg{
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

// Define the function to be called when ctrl-c (SIGINT) is sent to process
void signal_callback_handler(int signum)
{
  if(!gima)
  {
    exit(signum);
  }

  bool forced = signum != SIGINT || gima->isStopRequested();
  if(!forced)
  {
    gima->stop(false);
    std::cerr
      << "Termination requested after finishing the current encoding step..."
      << std::endl;
  }
  else
  {
    std::cerr << "Aborting process..." << std::endl;
    gima->stop(true);
    // Terminate program
    // exit(signum);
  }
}

namespace MediaArchiver
{
MediaArchiverClient::MediaArchiverClient(const ClientConfig &cfg)
  : m_cfg(cfg)
  , m_filter{"ffmpeg", 4u * 1024 * 1024 * 1024}
  , m_encodeProcess{nullptr, pclose}
  , m_authenticated(false)
  , m_stopRequested(false)
  , m_shutdown(false)
{
  std::random_device rd;
  std::mt19937 mt(rd());
  std::uniform_int_distribution<> dist(0, std::numeric_limits<int>::max());
  m_token = dist(mt);
}

MediaArchiverClient::~MediaArchiverClient()
{
  cleanUp();
}

void MediaArchiverClient::init() {}

void MediaArchiverClient::stop(bool forced)
{
  m_stopRequested = true;
  if(forced)
  {
    m_shutdown = true;
  }
}

template<>
bool MediaArchiverConfig<ClientConfig>::parse(
  const std::string &key, const std::string &value, ClientConfig &config)
{
  //  std::locale::global(std::locale(""));
  auto k = key;
  auto &f = std::use_facet<std::ctype<char>>(std::locale());
  f.tolower(&k[0], &k[0] + k.size());

  if(k == "serverconnectiontimeout")
  {
    config.serverConnectionTimeout = atoi(value.c_str());
  }
  else if(k == "verbosity")
  {
    config.verbosity = atoi(value.c_str());
  }
  else if(k == "checkfornewfileinterval")
  {
    config.checkForNewFileInterval = atoi(value.c_str());
  }
  else if(k == "reconnectdelay")
  {
    config.reconnectDelay = atoi(value.c_str());
  }
  else if(k == "serverport")
  {
    config.serverPort = atoi(value.c_str());
  }
  else if(k == "chunksize")
  {
    config.chunkSize = atol(value.c_str());
  }
  else if(k == "servername")
  {
    config.serverName = value;
  }
  else if(k == "pathtoencoder")
  {
    config.pathToEncoder = value;
  }
  else if(k == "pathtoprobe")
  {
    config.pathToProbe = value;
  }
  else if(k == "tempfolder")
  {
    config.tempFolder = value;
  }
  else
  {
    return false;
  }

  return true;
}

void MediaArchiverClient::checkCreateRpc()
{
  if(!m_rpc || !m_rpc->isConnected())
  {
    m_rpc.reset(new ServerIf(m_cfg));
  }
}

void MediaArchiverClient::doAuth()
{
  if(m_stopRequested)
  {
    m_shutdown = true;
    return;
  }

  m_authenticated = false;
  try
  {
    checkCreateRpc();

    std::stringstream ss;
    ss << m_token;
    m_rpc->authenticate(ss.str());
    m_authenticated = true;
    m_mainState = m_prevMainState;
    m_prevMainState = m_mainState;
  }
  catch(const rpc::timeout &e)
  {
    std::cerr << "ERROR: " << e.what() << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    return;
  }
  catch(const rpc::rpc_error &e)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    std::cerr << "ERROR: " << e.what() << '\n';
    return;
  }
  catch(const rpc::system_error &e)
  {
    m_prevMainState = MainStates::Idle;
    m_mainState = MainStates::WaitForReconnect;
    m_timeToWait = m_cfg.serverConnectionTimeout;
    m_startTime = std::chrono::steady_clock::now();
  }
}

void MediaArchiverClient::doIdle()
{
  if(m_stopRequested)
  {
    m_shutdown = true;
    return;
  }
  bool error = false;
  std::exception exc;
  MainStates next = m_mainState;
  try
  {
    checkCreateRpc();

    if(!m_authenticated)
    {
      next = MainStates::Authenticateing;
    }
    else
    {
      auto newFile = m_rpc->getNextFile(m_filter, m_encSettings);
      if(newFile)
      {
        next = MainStates::Receiving;
        std::stringstream fname;
        fname << InTmpFileName << "." << m_encSettings.fileExtension;
        m_srcFile.open(
          fname.str(), std::ios_base::out | std::ios_base::binary);
        if(m_srcFile.fail())
        {
          std::stringstream ss;
          ss << "could not open file \"" << fname.str() << "\" for write";
          throw IOError(ss.str());
        }
      }
      else
      {
        m_rpc.reset();
        next = MainStates::WaitForFileCheck;
        m_timeToWait = m_cfg.checkForNewFileInterval;
        m_startTime = std::chrono::steady_clock::now();
      }
    }
  }
  catch(const rpc::system_error &e)
  {
    error = true;
    exc = e;
    next = MainStates::WaitForReconnect;
    m_timeToWait = m_cfg.serverConnectionTimeout;
    m_startTime = std::chrono::steady_clock::now();
  }
  catch(const rpc::rpc_error &e)
  {
    error = true;
    exc = e;
    next = MainStates::WaitForReconnect;
    m_timeToWait = m_cfg.reconnectDelay;
    m_startTime = std::chrono::steady_clock::now();
  }
  catch(const IOError &e)
  {
    error = true;
    exc = e;
    // todo: handle error
  }
  catch(const std::exception &e)
  {
    error = true;
    exc = e;
    next = MainStates::WaitForReconnect;
    m_timeToWait = m_cfg.reconnectDelay;
    m_startTime = std::chrono::steady_clock::now();
  }

  if(error)
  {
    m_authenticated = false;
    std::cerr << "ERROR: " << exc.what() << '\n';
  }

  if(next != m_mainState)
  {
    m_prevMainState = m_mainState;
    m_mainState = next;
  }
}

void MediaArchiverClient::doReceive()
{
  try
  {
    auto cont = m_rpc->readChunk(m_srcFile);
    if(!cont)
    {
      // EOF
      if(m_srcFile.tellp() != m_encSettings.fileLength)
      {
        throw NetworkError(
          "Received file size is different to one reported by server");
      }
      m_srcFile.close();
      m_rpc.reset();

      m_encResult = EncodingResultInfo{
        .result = EncodingResultInfo::EncodingResult::UnknownError,
        .fileLength = 0,
        .error = m_stdOut.str()};

      std::stringstream cmd;
      cmd << m_cfg.pathToEncoder << m_encSettings.commandLineParameters
          << "-i \"" << m_cfg.tempFolder << "/" << InTmpFileName << "."
          << m_encSettings.fileExtension << "-o \"" << m_cfg.tempFolder
          << "/" << OutTmpFileName << "." << m_encSettings.fileExtension
          << "\" 1>/dev/null 2>&1";

      launch(cmd.str());
      m_mainState = MainStates::WaitForEncodingFinished;
    }
  }
  catch(const std::exception &e)
  {
    std::cerr << e.what() << std::endl;
    m_srcFile.seekp(0, std::ios_base::beg);
    m_rpc->reset();
  }
}

void MediaArchiverClient::launch(const std::string &cmdLine)
{
  m_encodeProcess.reset(popen(cmdLine.c_str(), "r"));
  if(!m_encodeProcess)
  {
    throw std::runtime_error(
      std::string("could not start external command: ") + cmdLine);
  }
}

int MediaArchiverClient::waitForFinish(std::string &stdOut)
{
  std::array<char, 4096> buffer;
  std::stringstream ss;
  while(!std::feof(m_encodeProcess.get()))
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    auto rd = fgets(buffer.data(), buffer.size(), m_encodeProcess.get());
    ss << buffer.data();
  }

  auto retcode = pclose(m_encodeProcess.release());
  stdOut = ss.str();
  return retcode;
}

void MediaArchiverClient::doWait()
{
  if(m_stopRequested && m_prevMainState == MainStates::Idle)
  {
    m_shutdown = true;
    return;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(250));
  auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - m_startTime)
                .count();
  if(diff >= m_timeToWait)
  {
    m_mainState = m_prevMainState;
  }
}

int MediaArchiverClient::getMovieLength(const std::string &path)
{
  std::stringstream cmd;
  std::string stdOut;
  cmd << m_cfg.pathToProbe << "-i \"" << path << "\" 2>&1";

  launch(cmd.str());
  auto retcode = waitForFinish(stdOut);
  //        # "Duration: 00:00:36.93, start: 1.040000, bitrate: 16355 kb/s"
  // regex = re.compile("r(?:Duration:\s*)(\d*):(\d*):(\d*).(\d*)",
  // re.MULTILINE | re.IGNORECASE) matches = re.search(regex, finfo)
  std::smatch m;
  std::regex re("(?:Duration:\\s*)(\\d*):(\\d*):(\\d*).(\\d*)",
    std::regex_constants::ECMAScript | std::regex_constants::icase);

  if(retcode == 0 && std::regex_match(stdOut, m, re))
  {
    if(m.size() != 4)
    {
      return -1;
    }

    int mult[4] = {3600, 60, 1, 0};
    int dur = 0;
    for(int i = 0; i < 4; i++)
    { dur += mult[i] * atoi(m[i].str().c_str()); }
    return dur;
  }
  else
  {
    return -1;
  }
}

void MediaArchiverClient::doConvert()
{
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  std::array<char, 1024> buffer;
  auto rd = fgets(buffer.data(), buffer.size(), m_encodeProcess.get());

  if(std::feof(m_encodeProcess.get()) || m_shutdown)
  {
    // EOF
    int retcode = -1;
    if(m_shutdown)
      m_encodeProcess.reset();
    else
      retcode = pclose(m_encodeProcess.release());

    // std::this_thread::sleep_for(std::chrono::seconds(1));
    if(retcode == 0)
    {
      do
      {
        std::stringstream cmd;
        cmd << m_cfg.pathToProbe << "\"" << m_cfg.tempFolder << "/"
            << OutTmpFileName << "." << m_encSettings.fileExtension << "\"";
        int lenOut = getMovieLength(cmd.str());

        if(lenOut <= 0)
        {
          std::cerr << "ERROR: output file size is 0 or probe error: "
                    << lenOut << std::endl;
          break;
        }

        cmd.clear();
        cmd << m_cfg.pathToProbe << "\"" << m_cfg.tempFolder << "/"
            << InTmpFileName << "." << m_encSettings.fileExtension << "\"";
        int lenIn = getMovieLength(cmd.str());

        if(lenOut != lenIn)
        {
          std::cerr << "ERROR: stream duration is different: " << lenIn
                    << " != " << lenOut << std::endl;
          break;
        }

        m_dstFile.open(m_cfg.tempFolder + "/" + OutTmpFileName,
          std::ios_base::in | std::ios_base::binary);

        if(!m_dstFile.is_open())
        {
          std::cerr << "ERROR: could not open output file for getting length"
                    << std::endl;
          break;
        }

        m_dstFile.seekg(0, std::ios_base::end);
        m_encResult.fileLength = m_dstFile.tellg();
        // leave file open for transmission stage

        // everything ok, Connect to server and send status
        m_encResult.result = EncodingResultInfo::EncodingResult::OK;
        m_encResult.error.clear();

      } while(false);
    }
    m_mainState = MainStates::SendResult;
  }
  else
  {
    m_stdOut << buffer.data();
  }
}

void MediaArchiverClient::doSendResult()
{
  try
  {
    checkCreateRpc();
    m_rpc->postFile(m_encResult);
    if(m_encResult.result == EncodingResultInfo::EncodingResult::OK &&
      m_encResult.fileLength > 0)
    {
      m_dstFile.seekg(0, std::ios_base::beg);
      m_mainState = MainStates::Transmitting;
    }
    else
    { // no success
      std::cerr << "Notified server about failed encoding" << std::endl;
      cleanUp();
      m_mainState = MainStates::Idle;
    }
  }
  catch(const std::exception &e)
  {
    std::cerr << e.what() << '\n';
    m_timeToWait = m_cfg.reconnectDelay;
    m_startTime = std::chrono::steady_clock::now();
    m_prevMainState = m_mainState;
    m_mainState = MainStates::WaitForReconnect;
  }
}

void MediaArchiverClient::doTransmit()
{
  try
  {
    checkCreateRpc();

    DataChunk chunk(m_cfg.chunkSize);
    m_dstFile.read(chunk.data(), chunk.size());
    auto res = m_rpc->writeChunk(chunk);

    if(m_dstFile.eof())
    {
      if(res)
      {
        throw NetworkError(
          "Server still wants to receive data but end of local file has been reached");
      }
      cleanUp();
      m_mainState = MainStates::Idle;
    }
  }
  catch(const std::exception &e)
  {
    std::cerr << e.what() << '\n';
    m_dstFile.seekg(0, std::ios_base::beg);
    m_rpc->reset();
  }
}

void MediaArchiverClient::removeTempFiles()
{
  DIR *dir;
  struct dirent *ent;
  if((dir = opendir(m_cfg.tempFolder.c_str())) != nullptr)
  {
    while((ent = readdir(dir)) != nullptr)
    {
      printf("%s\n", ent->d_name);
      std::string s(ent->d_name);
      if(s.rfind(InTmpFileName, 0) == 0 || s.rfind(OutTmpFileName, 0) == 0)
      {
        std::remove(ent->d_name);
      }
    }
    closedir(dir);
  }
  else
  {
    /* could not open directory */
    std::cerr << "Could not open folder \"" << m_cfg.tempFolder
              << "\" for enumerating files" << std::endl;
  }
}

void MediaArchiverClient::cleanUp()
{
  if(m_encodeProcess)
    pclose(m_encodeProcess.release());

  if(m_srcFile.is_open())
    m_srcFile.close();

  if(m_dstFile.is_open())
    m_dstFile.close();

  removeTempFiles();
  m_rpc.reset();
}

int MediaArchiverClient::poll()
{
  switch(m_mainState)
  {
    case MainStates::Idle: doIdle(); break;
    case MainStates::Authenticateing: doAuth(); break;
    case MainStates::Receiving: doReceive(); break;
    case MainStates::WaitForConnect:
    case MainStates::WaitForFileCheck:
    case MainStates::WaitForReconnect: doWait(); break;
    case MainStates::WaitForEncodingFinished: doConvert(); break;
    case MainStates::SendResult: doSendResult(); break;
    case MainStates::Transmitting: doTransmit(); break;
    default: break;
  }

  if(m_shutdown)
  {
    if(m_rpc)
    {
      try
      {
        m_rpc->abort();
      }
      catch(const std::exception &e)
      {
        std::cerr << "Error during shutting down: " << e.what()
                  << std::endl;
      }

      m_rpc.reset();
    }
    cleanUp();
    return 1;
  }

  return 0;
}

}

int main(int argc, char **argv)
{
  // load configuration

  {
    auto mac =
      MediaArchiver::MediaArchiverConfig<MediaArchiver::ClientConfig>(gCfg);
    auto b = mac.read("MediaArchiver.cfg");
  }

  // Register signal and signal handler
  signal(SIGINT, signal_callback_handler);
  signal(SIGABRT, signal_callback_handler);

#ifndef WIN32
  signal(SIGKILL, signal_callback_handler);
#endif

  gima.reset(new MediaArchiver::MediaArchiverClient(gCfg));
  if(!gima)
    return 1;

  gima->init();
  int retcode = 0;
  while((retcode = gima->poll()) == 0) {}

  gima.reset();
  return retcode;
}
