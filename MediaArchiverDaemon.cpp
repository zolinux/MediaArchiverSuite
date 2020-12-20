#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <regex>
#include <locale>

#include "MediaArchiverConfig.hpp"
#include "MediaArchiverDaemonConfig.hpp"
#include "MediaArchiverDaemon.hpp"

#include "rpc/server.h"
#include "rpc/this_handler.h"
#include "rpc/this_session.h"
#include "rpc/this_server.h"

#include "IMediaArchiverServer.hpp"
#include "RpcFunctions.hpp"

#include "FileSystemWatcher.hpp"
#include "SQLite.hpp"

#include "loguru.hpp"

#ifdef WIN32
  #include "FileCopierWindows.hpp"
  #define FileCopier FileCopierWindows
#else
  #include "FileCopierLinux.hpp"
  #define FileCopier FileCopierLinux
#endif
static std::unique_ptr<MediaArchiver::MediaArchiverDaemon> gima;

static MediaArchiver::DaemonConfig gCfg{
  .verbosity = 0,
  .serverPort = 2020,
  .aBitRate = 80000,
  .crf = 22,
  .chunkSize = 256 * 1024,
  .foldersToWatch = "",
  .filenameMatchPattern = std::regex(
    "\\.(mp4|3gp|mov|avi|mts|vob|ts|mpg|mpe|mpeg|divx|qt|wmv|asf|flv)$",
    std::regex::ECMAScript | std::regex::icase),
  .vCodec = "libx265",
  .aCodec = "aac",
#ifdef WIN32
  .tempFolder = "",
#else
  .tempFolder = "",
#endif
  .finalExtension = ".mp4",
#ifdef WIN32
  .dbPath = "MediaArchiver.db",
#else
  .dbPath = "/var/cache/MediaArchiver/MediaArchiver.db",
#endif
  .resultFileSuffix = "_archived",
};

namespace
{
const char gNotAuthenticatedError[] = "Client not authenticated!";
}

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
    LOG_F(WARNING,
      "Termination requested after finishing the current encoding step...");
    gima->stop(false);
  }
  else
  {
    LOG_F(WARNING, "Aborting process...");
    gima->stop(true);
  }
}

namespace MediaArchiver
{
void MediaArchiverDaemon::init()
{
  m_connections.clear();
  m_stopRequested = false;
}

void MediaArchiverDaemon::start()
{
  LOG_F(1, "Starting service");
  try
  {
    m_srv.async_run(1);
  }
  catch(const std::exception &e)
  {
    LOG_F(FATAL, "Could not start service: %s", e.what());
    stop(true);
    return;
  }

  std::unique_lock<std::mutex> lck(m_mtxFileMove);
  while(!m_stopRequested || !isIdle())
  {
    if(m_filesToMove.size() == 0)
    {
      m_cv.wait(lck);
    }

    if(m_stopRequested && isIdle())
    {
      break;
    }

    if(m_filesToMove.size() == 0)
    {
      continue;
    }

    const FileToMove ftm = std::move(m_filesToMove.front());
    m_filesToMove.pop_front();

    LOG_F(INFO, "Archive File of id %u(%s) ready to move",
      ftm.result.originalFileId, ftm.result.fileName.c_str());

    if(!ftm.result.originalFileId)
    {
      continue;
    }
    lck.unlock();
    std::string error;

    if(ftm.result.result != EncodingResultInfo::EncodingResult::OK ||
      ftm.result.fileLength == 0)
    {
      LOG_F(ERROR, "Process (file id: %u) resulted in error %i: %s",
        ftm.result.originalFileId, ftm.result.result,
        ftm.result.error.c_str());
      m_db.addEncodedFile(ftm.result);
    }
    else
    {
      try
      {
        FileCopier().moveFile(
          ftm.tmp.c_str(), ftm.result.fileName.c_str(), &ftm.atime);
        LOG_F(1, "File %u '%s' was moved to place",
          ftm.result.originalFileId, ftm.result.fileName.c_str());
        m_db.addEncodedFile(ftm.result);
      }
      catch(const std::exception &e)
      {
        LOG_F(ERROR, "File %u (%s) move error: %s",
          ftm.result.originalFileId, ftm.result.fileName.c_str(), e.what());
        error = e.what();
      }
    }

    if(error.empty()) {}
    else
    {
      auto errResult = ftm.result;
      errResult.error = error;
      errResult.fileLength = 0;
      errResult.result = static_cast<int8_t>(
        EncodingResultInfo::EncodingResult::ServerIOError);

      m_db.addEncodedFile(errResult);
      // ToDo: delete temporary file
    }

    // lock mutex again for next cycle
    lck.lock();
  }

  m_srv.stop();
}

bool MediaArchiverDaemon::isIdle()
{
  std::lock_guard<std::mutex> lck(m_mtxFileMove);
  for(const auto &c: m_connections)
  {
    if(c.second.inFile.is_open() || c.second.outFile.is_open())
    {
      return false;
    }
  }
  return m_filesToMove.size() == 0;
}

void MediaArchiverDaemon::stop(bool forced)
{
  m_stopRequested = true;
  LOG_F(1, "%s stopping requested", forced ? "FORCED" : "NORMAL");

  if(forced) {}
}

MediaArchiverDaemon::MediaArchiverDaemon(
  const DaemonConfig &cfg, IDatabase &db)
  : m_cfg(cfg)
  , m_db(db)
  , m_srv(cfg.serverPort)
{
  init();

  m_srv.bind(RpcFunctions::getVersion, []() -> uint32_t {
    auto id = rpc::this_session().id();
    LOG_F(INFO, "getVersion requested (%li)", id);
    return 1;
  });

  m_srv.bind(RpcFunctions::authenticate, [&](const string &token) -> void {
    // set thread name if not yet done
    auto id = rpc::this_session().id();
    stringstream ss;
    ss << "RPC_" << std::hex << id;
    loguru::set_thread_name(ss.str().c_str());

    LOG_F(INFO, "Auth requested (%li, %X): %s", id, this_thread::get_id(),
      token.c_str());
    this->authenticate(token);
  });

  m_srv.bind(RpcFunctions::reset, [&]() -> void {
    LOG_F(INFO, "Reset transmission requested (%li)s",
      rpc::this_session().id());
    this->reset();
  });

  m_srv.bind(RpcFunctions::abort, [&]() -> void {
    LOG_F(INFO, "Abort requested (%li)", rpc::this_session().id());
    this->abort();
  });

  m_srv.bind(RpcFunctions::getNextFile,
    [&](const MediaFileRequirements &filter) -> MediaEncoderSettings {
      LOG_SCOPE_F(2, "getNextFile");
      MediaEncoderSettings settings;
      settings.fileLength = 0;

      // the loop if present to handle an event that the requested file
      // cannot be opened for some reason. This error should not propagate
      // to client, but simply another file should be taken.
      do
      {
        try
        {
          auto &cli = checkClient();

          getNextFile(cli, filter, settings);
          break;
        }
        catch(const std::exception &e)
        {
          LOG_F(ERROR, "getNextFile error: %s", e.what());
          // if(!string(e.what()).compare(0,
          // sizeof(::gNotAuthenticatedError),
          //      ::gNotAuthenticatedError))
          {
            rpc::this_handler().respond_error(e.what());
            settings.fileLength = 0;
            break;
          }
        }
      } while(true);

      return settings;
    });

  m_srv.bind(
    RpcFunctions::postFile, [&](const EncodingResultInfo &result) -> void {
      LOG_F(3, "postFile: %i, %luBytes, %s", result.result,
        result.fileLength, result.error.c_str());
      try
      {
        this->postFile(result);
      }
      catch(const std::exception &e)
      {
        LOG_F(ERROR, "PostFile: %s", e.what());
        rpc::this_handler().respond_error(
          std::string("I/O error") + e.what());
      }
    });

  m_srv.bind(RpcFunctions::writeChunk, [&](const DataChunk &chunk) -> bool {
    bool ret = false;
    try
    {
      ret = this->writeChunk(chunk);
    }
    catch(const std::exception &e)
    {
      LOG_F(
        ERROR, "WriteChunk (%li): %s", rpc::this_session().id(), e.what());
      rpc::this_handler().respond_error(
        std::string("I/O error") + e.what());
    }
    return ret;
  });

  m_srv.bind(RpcFunctions::readChunk, [&]() -> tuple<bool, DataChunk> {
    DataChunk chunk(m_cfg.chunkSize);
    bool haveMore = false;
    try
    {
      haveMore = this->readChunk(chunk);
    }
    catch(const std::exception &e)
    {
      LOG_F(
        ERROR, "ReadChunk (%li): %s", rpc::this_session().id(), e.what());
      rpc::this_handler().respond_error(
        std::string("I/O error:") + e.what());
    }

    return make_tuple(haveMore, std::move(chunk));
  });
}

MediaArchiverDaemon::~MediaArchiverDaemon() {}

template<>
bool MediaArchiverConfig<DaemonConfig>::parse(
  const std::string &key, const std::string &value, DaemonConfig &config)
{
  auto k = key;
  auto &f = std::use_facet<std::ctype<char>>(std::locale());
  f.tolower(&k[0], &k[0] + k.size());

  if(k == "folderstowatch")
  {
    config.foldersToWatch = value;
  }
  else if(k == "filenamematchpattern")
  {
    config.filenameMatchPattern = std::regex(
      value.c_str(), std::regex::ECMAScript | std::regex::icase);
  }
  else if(k == "vcodec")
  {
    config.vCodec = value;
  }
  else if(k == "acodec")
  {
    config.aCodec = value;
  }
  else if(k == "tempfolder")
  {
    config.tempFolder = value;
  }
  else if(k == "finalextension")
  {
    config.finalExtension = value;
  }
  else if(k == "dbpath")
  {
    config.dbPath = value;
  }
  else if(k == "resultfilesuffix")
  {
    config.resultFileSuffix = value;
  }
  else if(k == "logfile")
  {
    config.logFile = value;
  }
  else if(k == "verbosity")
  {
    config.verbosity = atoi(value.c_str());
  }
  else if(k == "serverport")
  {
    config.serverPort = atoi(value.c_str());
  }
  else if(k == "abitrate")
  {
    config.aBitRate = atoi(value.c_str());
  }
  else if(k == "crf")
  {
    config.crf = atoi(value.c_str());
  }
  else if(k == "chunksize")
  {
    config.chunkSize = atoi(value.c_str());
  }
  else
    return false;

  return true;
}

bool MediaArchiverDaemon::isArchive(const std::string &fileName) const
{
  return fileName.find(m_cfg.resultFileSuffix, 0) != string::npos &&
    fileName.find(m_cfg.finalExtension,
      fileName.size() - m_cfg.finalExtension.size()) != string::npos;
}
bool MediaArchiverDaemon::isInterestingFile(
  const std::string &fileName) const
{
  return regex_search(fileName.c_str(), m_cfg.filenameMatchPattern);
}

void MediaArchiverDaemon::onFileSystemChange(
  IFileSystemChangeListener::EventType e, const std::string &src,
  const std::string &dst)
{
  if(e == IFileSystemChangeListener::EventType::FileDeleted)
    return;

  size_t size[2];

  if(!dst.empty())
  {
    size[1] = FileCopier().getFileSize(dst.c_str());
    LOG_F(4, "dst size: %lu", size[1]);
  }
  else
    return;

  bool dstIsArchive = false;
  string aName;
  size_t aSize = 0;

  if(isArchive(dst))
  {
    // an archived file has been created in a watched folder, todo: look for
    // original file BasicFileInfo f{.fileName = dst, .fileSize = size[1]};
    // m_db.addFile(nullptr, &f, false);
    dstIsArchive = true;
    aName = dst;
    const size_t pos = aName.rfind(m_cfg.resultFileSuffix);
    const size_t len = m_cfg.resultFileSuffix.length();
    aName.erase(pos, len);
  }

  if(isInterestingFile(dst))
  {
    LOG_F(5, "onFileSystemChange: e=%i, src=%s, dst=%s",
      static_cast<int>(e), src.c_str(), dst.c_str());
    if(dstIsArchive) {}
    else
    {
      // dst is not archive -> must be source. Need to search for archive
      aName = getArchivedFileName(dst);
    }
  }
  else if(!dstIsArchive)
  {
    return;
  }

  // get the file size of the potential counterpart - if present
  if(!aName.empty())
  {
    try
    {
      aSize = FileCopier().getFileSize(aName.c_str());
    }
    catch(const std::exception &)
    {
    }
  }

  BasicFileInfo fs{.fileName = dstIsArchive ? aName : dst,
    .fileSize = dstIsArchive ? aSize : size[1]};

  BasicFileInfo fd{.fileName = dstIsArchive ? dst : aName,
    .fileSize = dstIsArchive ? size[1] : aSize};

  switch(e)
  {
    case IFileSystemChangeListener::EventType::FileDiscovered:
    case IFileSystemChangeListener::EventType::FileCreated:
    case IFileSystemChangeListener::EventType::FileMoved:
      // put file to database
      m_db.addFile(!dstIsArchive || (dstIsArchive && aSize) ? &fs : nullptr,
        dstIsArchive || (!dstIsArchive && aSize) ? &fd : nullptr,
        !dstIsArchive || aSize);
      break;

    default: break;
  }
}

void MediaArchiverDaemon::authenticate(const std::string &token)
{
  auto id = rpc::this_session().id();
  ConnectedClient cl;
  std::lock_guard<std::mutex> lck(m_mtxFileMove);

  for(auto conn = m_connections.begin(); conn != m_connections.end();)
  {
    if(token == conn->second.token)
    {
      if(!cl.token.empty())
      {
        LOG_F(ERROR,
          "authenticate: Multiple connections with identical token!");
      }
      cl = std::move(conn->second);
      conn = m_connections.erase(conn);
    }
    else
    {
      ++conn;
    }
  }

  cl.lastActivify = std::chrono::steady_clock::now();
  cl.token = token;
  m_connections[id] = std::move(cl);
}
uint32_t MediaArchiverDaemon::getVersion() const
{
  return 1;
}
bool MediaArchiverDaemon::isConnected() const
{
  return false;
}

void MediaArchiverDaemon::reset()
{
  auto &cli = checkClient();
  if(cli.inFile.is_open())
  {
    cli.inFile.seekg(0, ios_base::seekdir::_S_beg);
  }
  else if(cli.outFile.is_open())
  {
    cli.outFile.seekp(0, ios_base::seekdir::_S_beg);
  }
}
void MediaArchiverDaemon::abort()
{
  auto &cli = checkClient();
  if(cli.inFile.is_open())
  {
    cli.inFile.close();
  }
  else if(cli.outFile.is_open())
  {
    cli.outFile.close();
  }
  else
    return;

  lock_guard<mutex> lck(m_mtxFileMove);
  m_db.reset(cli.originalFileId);
  cli.inFile = ifstream();
  cli.outFile = ofstream();
  cli.originalFileId = 0;
  cli.tempFileName = "";
  cli.originalFileName = "";
  cli.encSettings = MediaEncoderSettings{.fileLength = 0};
  cli.encResult = EncodingResultInfo();
}

bool MediaArchiverDaemon::getNextFile(ConnectedClient &cli,
  const MediaFileRequirements &filter, MediaEncoderSettings &settings)
{
  if(cli.inFile.is_open())
  {
    stringstream ss;
    ss << "The file " << cli.originalFileId << " <" << cli.originalFileName
       << "> is already open for reading";
    throw std::runtime_error(ss.str());
  }

  if(cli.outFile.is_open())
  {
    stringstream ss;
    ss << "The file " << cli.originalFileId << " <" << cli.tempFileName
       << "> is already open for writing";
    throw std::runtime_error(ss.str());
  }
  cli.originalFileId = 0;
  cli.filter = filter;
  uint32_t srcId = 0;

  cli.encSettings.fileLength = 0;
  cli.originalFileName.clear();

  if(!m_stopRequested)
  {
    BasicFileInfo fi;
    fi.fileSize = 0;

    srcId = m_db.getNextFile(cli.filter, fi);
    if(srcId > 0)
    {
      if(!fi.fileSize)
      {
        stringstream ss;
        ss << "File <" << fi.fileName << "> skipped due to 0 length";
        throw runtime_error(ss.str());
      }

      ifstream inFile(fi.fileName, ios::in);
      if(!inFile.is_open())
      {
        throw IOError(string("Could not open file: ") + fi.fileName);
      }
      FileCopier().getFileTimes(fi.fileName.c_str(), cli.times);
      cli.inFile = move(inFile);
      cli.encSettings.fileLength = fi.fileSize;
      cli.originalFileName = fi.fileName;
      stringstream ss;
      ss << "-y -hide_banner -nostats -loglevel warning -copyts -map_metadata 0 -movflags use_metadata_tags -preset veryfast -c:v "
         << m_cfg.vCodec << " -c:a " << m_cfg.aCodec << " -crf "
         << m_cfg.crf << " -b:a " << to_string(m_cfg.aBitRate);
      cli.encSettings.commandLineParameters = ss.str();
    }
  }

  cli.originalFileId = srcId;
  cli.encSettings.encoderType = filter.encoderType;

  auto posExt = cli.originalFileName.find_last_of('.');
  cli.encSettings.fileExtension = cli.originalFileName.substr(posExt + 1);
  cli.encSettings.finalExtension = m_cfg.finalExtension;
  settings = cli.encSettings;

  if(srcId > 0)
  {
    LOG_F(INFO, "Next file to process %u (%s)", cli.originalFileId,
      cli.originalFileName.c_str());
    return true;
  }
  else
  {
    LOG_F(1, "No files found to process");
    return false;
  }
}

/**
 * @brief read a chunf rom the file. if error, it returns false on error,
 * otherwise true. EOF is reached if the data size != chunk size
 *
 * @param chunk
 * @return true read OK
 * @return false i/o error
 */
bool MediaArchiverDaemon::readChunk(DataChunk &chunk)
{
  auto &cli = checkClient();

  if(cli.inFile.is_open())
  {
    auto len = cli.inFile.readsome(chunk.data(), m_cfg.chunkSize);
    // do close file later in case data must be repeated (rewind needed)
    // if(cli.inFile.eof())
    // {
    //   cli.inFile.close();
    // }
    auto error = cli.inFile.fail() && !cli.inFile.eof();
    if(error)
    {
      throw IOError("Cannot read from file");
    }
    else
    {
      if(chunk.size() != len)
      {
        chunk.resize(len);
      }
    }
    return len;
  }
  else
  {
    throw IOError("No file is open for read");
  }
}

void MediaArchiverDaemon::postFile(const EncodingResultInfo &result)
{
  auto &cli = checkClient();
  if(cli.inFile.is_open())
  {
    if(cli.inFile.tellg() != cli.encSettings.fileLength)
    {
      throw runtime_error("File not read till the end");
    }

    cli.inFile.close();
  }

  if(cli.outFile.is_open())
  {
    throw std::runtime_error("Output file is still open");
  }

  cli.encResult = result;
  if(result.result == EncodingResultInfo::EncodingResult::OK &&
    result.fileLength > 0)
  {
    // prepare for receiving data
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    if(m_cfg.tempFolder == ".")
    {
      ss << cli.originalFileName << "." << cli.originalFileId;
    }
    else if(m_cfg.tempFolder.empty())
    {
      ss << "./" << cli.originalFileId;
    }
    else
    {
      ss << m_cfg.tempFolder << '/' << cli.originalFileId;
    }

    cli.tempFileName = ss.str();
    cli.outFile.open(cli.tempFileName, std::ios::binary | std::ios::out);
    if(!cli.outFile.is_open())
    {
      throw std::runtime_error(
        string("Could not open output temp file: ") + cli.tempFileName);
    }
  }
  else
  {
    // error during encoding, no data will be received
    LOG_F(ERROR, "Encoding failed for id %u (%s)", cli.originalFileId,
      cli.originalFileName.c_str());
    std::lock_guard<std::mutex> lck(m_mtxFileMove);
    prepareNewSession(cli);
    m_cv.notify_all();
  }
}
bool MediaArchiverDaemon::writeChunk(const std::vector<char> &data)
{
  auto &cli = checkClient();
  if(!cli.originalFileId || !cli.outFile.is_open() ||
    !cli.encResult.fileLength ||
    cli.outFile.tellp() + data.size() > cli.encResult.fileLength)
  {
    LOG_F(ERROR,
      "writeChunk: state: id=%u, outFile=%s, resultLength=%lu, overrun=%i",
      cli.originalFileId, cli.outFile.is_open() ? "OPEN" : "CLOSED",
      cli.encResult.fileLength,
      cli.outFile.tellp() + data.size() > cli.encResult.fileLength);

    throw std::runtime_error("writeChunk: invalid state");
  }

  cli.outFile.write(data.data(), data.size());
  if(cli.outFile.tellp() < cli.encResult.fileLength)
  {
    // copying not finished yet
    return true;
  }
  {
    LOG_F(INFO, "writeChunk: Copying finished, file can be moved");
    std::lock_guard<std::mutex> lck(m_mtxFileMove);
    cli.outFile.close();

    // add file to queue for moving it to place in main thread
    prepareNewSession(cli);

    // send signal to main loop to start moving file...
    m_cv.notify_all();
  }
  return false;
}

std::string MediaArchiverDaemon::getArchivedFileName(
  const std::string &origFileName) const
{
  auto posExt = origFileName.find_last_of('.');
  auto newFileName = origFileName;
  if(posExt == std::string::npos)
  {
    posExt = origFileName.length();
  }
  else
  {
    newFileName.resize(posExt);
  }

  newFileName += m_cfg.resultFileSuffix;
  newFileName += m_cfg.finalExtension;

  return newFileName;
}

void MediaArchiverDaemon::prepareNewSession(ConnectedClient &cli)
{
  m_filesToMove.emplace_back(
    FileToMove{.result = EncodedFile(cli.encResult, cli.originalFileId,
                 getArchivedFileName(cli.originalFileName)),
      .tmp = cli.tempFileName,
      .atime = cli.times[0],
      .mtime = cli.times[1]});

  LOG_F(2, "prepare session after #%u %s", cli.originalFileId,
    cli.encResult.result == EncodedFile::EncodingResult::OK ? "SUCCEEDED" :
                                                              "FAILED");
  // preparing the next file transfer
  cli.originalFileId = 0;
  cli.tempFileName = "";
  cli.originalFileName = "";
  cli.encSettings = MediaEncoderSettings{.fileLength = 0};
  cli.encResult = EncodingResultInfo();
  cli.inFile = ifstream();
  cli.outFile = ofstream();
  memset(cli.times, 0, sizeof(cli.times));
}

ConnectedClient &MediaArchiverDaemon::checkClient()
{
  try
  {
    auto &cli = m_connections.at(rpc::this_session().id());
    return cli;
  }
  catch(const std::exception &e)
  {
    LOG_F(ERROR, "Client not authenticated!");
    throw;
  }
}
}

int main(int argc, char **argv)
{
  std::string cfgFileName = "MediaArchiver.cfg";
  std::string logFile;
  int c;
  auto v = loguru::Verbosity_INFO;
  bool verbosityOverridden = false;
  bool showHelp = false;
  bool doFork = true;

  while((c = getopt(argc, argv, "c:l:nv:h")) != -1)
  {
    switch(c)
    {
      case 'c': cfgFileName = optarg; break;
      case 'l': logFile = optarg; break;
      case 'v':
        verbosityOverridden = true;
        v = static_cast<loguru::NamedVerbosity>(atoi(optarg));
        break;
      case 'n': doFork = false; break;
      case 'h': showHelp = true; break;

      default: break;
    }
  }

  if(showHelp)
  {
    cout
      << "Usage: " << argv[0]
      << " [-nh] [-v N] [-c configFile] [-l logfile]" << endl
      << "\t-n\tno fork, process remains in foreground" << endl
      << "\t-v\tverbosity level (-9 fatal -> 0 info -> 9 all)" << endl
      << "\t-h\tshow this help" << endl
      << "\t-c\tconfig file to use, by default MediaArchiver.cfg is used in local folder"
      << endl
      << "\t-l\tlog output file, override config file value" << endl;
    return -1;
  }

  loguru::g_internal_verbosity = 1;
  loguru::SignalOptions sigOpts;
  loguru::Options opts;

  sigOpts.sigint = false;
  opts.signals = sigOpts;
  loguru::init(argc, argv, opts);

  MediaArchiver::MediaArchiverConfig<MediaArchiver::DaemonConfig> mac(gCfg);

  try
  {
    mac.read(cfgFileName);
  }
  catch(const std::exception &e)
  {
    LOG_F(ERROR, e.what());
    return 1;
  }

  // override value from config file
  if(logFile.empty())
  {
    logFile = gCfg.logFile;
  }

  if(!verbosityOverridden)
  {
    v = static_cast<loguru::NamedVerbosity>(gCfg.verbosity);
  }

  if(!logFile.empty())
  {
    if(!loguru::add_file(logFile.c_str(), loguru::FileMode::Append, v))
    {
      LOG_F(ERROR, "Cannot open logfile '%s' to write", logFile.c_str());
      return 1;
    }
  }

  std::list<std::string> folders;
  char pathSeparator = ':';
#ifdef WIN32
  pathSeparator = ';';
#endif
  std::istringstream iss(gCfg.foldersToWatch);

  for(std::string buf; std::getline(iss, buf, pathSeparator);)
  {
    LOG_F(2, "Watching folder '%s'", buf.c_str());
    folders.emplace_back(move(buf));
  }

  if(folders.size() < 1)
  {
    LOG_F(ERROR, "Missing media folder(s)");
    return 1;
  }

  if(doFork)
  {
    switch(fork())
    {
      case -1: LOG_F(ERROR, "Could not fork: %i", errno); return 2;
      case 0: break;
      default: return 0;
    }
  }

  MediaArchiver::SQLite db;
  db.init();
  db.connect(gCfg.dbPath.c_str(), true);

  gima.reset(new MediaArchiver::MediaArchiverDaemon(gCfg, db));
  std::unique_ptr<MediaArchiver::IFileSystemWatcher> fsw(
    MediaArchiver::FileSystemWatcher::create(*gima.get(), folders));

  gima->start();
  LOG_F(INFO, "Exiting...");
  fsw.reset();
  gima.reset();
  return 0;
}
