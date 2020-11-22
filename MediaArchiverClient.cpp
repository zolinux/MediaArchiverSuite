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
#include <regex>
#include <random>

#ifdef _MSVC_STL_VERSION
#else
  #include <dirent.h>
#endif

#include "rpc/client.h"
#include "rpc/rpc_error.h"
#include "MediaArchiverClient.hpp"
#include "MediaArchiverConfig.hpp"
#include "ServerIf.hpp"

#include "loguru.hpp"

using namespace MediaArchiver;

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
  LOG_F(INFO, "%s stop requested", forced ? "FORCED" : "NORMAL");
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
    LOG_F(1, "Auth OK");
  }
  catch(const rpc::timeout &e)
  {
    LOG_F(ERROR, "server timeout");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    return;
  }
  catch(const rpc::rpc_error &e)
  {
    LOG_F(ERROR, "server error: %s", e.what());
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
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
  std::exception exc;
  std::string errStr;
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
    errStr = e.what();
    next = MainStates::WaitForReconnect;
    m_timeToWait = m_cfg.serverConnectionTimeout;
    m_startTime = std::chrono::steady_clock::now();
  }
  catch(const rpc::rpc_error &e)
  {
    errStr = e.what();
    next = MainStates::WaitForReconnect;
    m_timeToWait = m_cfg.reconnectDelay;
    m_startTime = std::chrono::steady_clock::now();
  }
  catch(const IOError &e)
  {
    errStr = e.what();
    // todo: handle error
  }
  catch(const std::exception &e)
  {
    errStr = e.what();
    next = MainStates::WaitForReconnect;
    m_timeToWait = m_cfg.reconnectDelay;
    m_startTime = std::chrono::steady_clock::now();
  }

  if(!errStr.empty())
  {
    m_authenticated = false;
    LOG_F(ERROR, "doIdle: %s", errStr.c_str());
  }

  if(next != m_mainState)
  {
    LOG_F(2, "State change: %i -> %i -> %i", (int)m_prevMainState,
      (int)m_mainState, (int)next);
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
      cmd << m_cfg.pathToEncoder << " -y -hide_banner -i \""
          << m_cfg.tempFolder << "/" << InTmpFileName << "."
          << m_encSettings.fileExtension << "\" "
          << m_encSettings.commandLineParameters << " \""
          << m_cfg.tempFolder << "/" << OutTmpFileName
          << m_encSettings.finalExtension << "\" 2>&1";

      launch(cmd.str());
      m_mainState = MainStates::WaitForEncodingFinished;
    }
  }
  catch(const std::exception &e)
  {
    LOG_F(ERROR, "doReceive: %s", e.what());
    m_srcFile.seekp(0, std::ios_base::beg);
    m_rpc->reset();
  }
}

void MediaArchiverClient::launch(const std::string &cmdLine)
{
  LOG_F(2, "launching: %s", cmdLine.c_str());
  auto handle = popen(cmdLine.c_str(), "r");
  if(!handle)
  {
    LOG_F(ERROR, "could not launch '%s'", cmdLine.c_str());
    throw std::runtime_error(
      std::string("could not start external command: ") + cmdLine);
  }

  std::this_thread::sleep_for(std::chrono::seconds(2));
  m_encodeProcess.reset(handle);
}

int MediaArchiverClient::waitForFinish(std::string &stdOut)
{
  std::array<char, 4096> buffer;
  std::stringstream ss;
  while(!std::feof(m_encodeProcess.get()))
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    memset(buffer.data(), 0, buffer.size());
    auto rd = fgets(buffer.data(), buffer.size(), m_encodeProcess.get());
    ss << buffer.data();
  }

  auto retcode = pclose(m_encodeProcess.release());
  stdOut = ss.str();
  VLOG_F(retcode ? -2 : 2, "waitForFinish: return: %i, <%s>", retcode,
    stdOut.c_str());
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
  cmd << m_cfg.pathToProbe << " -i \"" << path << "\" 2>&1";

  launch(cmd.str());
  auto retcode = waitForFinish(stdOut);
  //        # "Duration: 00:00:36.93, start: 1.040000, bitrate: 16355 kb/s"
  // regex = re.compile("r(?:Duration:\s*)(\d*):(\d*):(\d*).(\d*)",
  // re.MULTILINE | re.IGNORECASE) matches = re.search(regex, finfo)
  std::smatch m;
  std::regex re("(?:Duration:\\s*)(\\d*):(\\d*):(\\d*).(\\d*)",
    std::regex_constants::ECMAScript | std::regex_constants::icase);

  if(retcode == 0 && std::regex_search(stdOut, m, re))
  {
    const int mult[] = {3600, 60, 1, 0};
    const int timeCount = sizeof(mult) / sizeof(mult[0]);

    if(m.size() != timeCount + 1)
    {
      LOG_F(ERROR, "getMovieLength: Invalid output (%lu)", m.size());
      return -1;
    }

    int dur = 0;
    for(int i = 0; i < timeCount; i++)
    {
      auto tim = atoi(m[i + 1].str().c_str());
      dur += mult[i] * tim;
    }
    LOG_F(1, "getMovieLength: duration (%u)", dur);
    return dur;
  }
  else
  {
    LOG_F(ERROR, "getMovieLength!");
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
    {
      LOG_F(INFO, "doConvert: stopping encoding due to stop request");
      m_encodeProcess.reset();
    }
    else
    {
      LOG_F(INFO, "doConvert: closing encoding process...");
      retcode = pclose(m_encodeProcess.release());
    }

    // std::this_thread::sleep_for(std::chrono::seconds(1));
    if(retcode == 0)
    {
      try
      {
        std::stringstream cmd;
        cmd << m_cfg.tempFolder << "/" << OutTmpFileName
            << m_encSettings.finalExtension;
        std::string outFile = cmd.str();
        int lenOut = getMovieLength(outFile);

        if(lenOut <= 0)
        {
          LOG_F(ERROR, "output file size is 0 or probe error: %i", lenOut);
          throw std::runtime_error("1");
        }

        cmd = std::stringstream();
        cmd << m_cfg.tempFolder << "/" << InTmpFileName << "."
            << m_encSettings.fileExtension;
        int lenIn = getMovieLength(cmd.str());

        if(lenOut != lenIn)
        {
          LOG_F(
            ERROR, "stream duration is different: %i != %i", lenIn, lenOut);
          throw std::runtime_error("2");
        }

        m_dstFile.open(outFile, std::ios::in | std::ios::binary);

        if(!m_dstFile.is_open())
        {
          LOG_F(ERROR, "could not open output file for getting length");
          throw std::runtime_error("3");
        }

        m_dstFile.seekg(0, std::ios::end);
        m_encResult.fileLength = m_dstFile.tellg();
        // leave file open for transmission stage

        // everything ok, Connect to server and send status
        m_encResult.result = EncodingResultInfo::EncodingResult::OK;
        m_encResult.error.clear();
        LOG_F(INFO, "doConvert: File opened to stream to server");
      }
      catch(std::exception &e)
      {
        LOG_F(ERROR, "doConvert: %s", e.what());
      }
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
      cleanUp();
      m_mainState = MainStates::Idle;
    }
  }
  catch(const std::exception &e)
  {
    LOG_F(ERROR, "doSendResult: %s", e.what());
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
    LOG_F(ERROR, "doTransmit: %s", e.what());
    m_dstFile.seekg(0, std::ios_base::beg);
    m_rpc->reset();
  }
}

void MediaArchiverClient::removeTempFiles()
{
#ifdef _MSVC_STL_VERSION
  HANDLE dir;
  WIN32_FIND_DATA file_data;

  if((dir = FindFirstFile((m_cfg.tempFolder + "/*").c_str(), &file_data)) ==
    INVALID_HANDLE_VALUE)
    return; /* No files found */

  do
  {
    const std::string file_name = file_data.cFileName;
    const std::string full_file_name = m_cfg.tempFolder + "/" + file_name;
    const bool is_directory = (file_data.dwFileAttributes &
                                FILE_ATTRIBUTE_DIRECTORY) != 0;

    if(file_name[0] == '.')
      continue;

    if(is_directory)
      continue;

    LOG_F(1, "Removing Temp file: %s", ent->d_name);

  } while(FindNextFile(dir, &file_data));

  FindClose(dir);
#else
  DIR *dir;
  struct dirent *ent;
  if((dir = opendir(m_cfg.tempFolder.c_str())) != nullptr)
  {
    while((ent = readdir(dir)) != nullptr)
    {
      std::string s(ent->d_name);
      if(s.rfind(InTmpFileName, 0) == 0 || s.rfind(OutTmpFileName, 0) == 0)
      {
        LOG_F(1, "Removing Temp file: %s", ent->d_name);
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
#endif
}

void MediaArchiverClient::cleanUp()
{
  LOG_F(INFO, "Cleaning up...");
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
        LOG_F(ERROR, "Error during shutting down: %s", e.what());
      }

      m_rpc.reset();
    }
    cleanUp();
    return 1;
  }

  return 0;
}
