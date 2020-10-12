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
void MediaArchiverDaemon::init()
{
  m_connections.clear();
  m_stopRequested = false;
}

void MediaArchiverDaemon::start()
{
  try
  {
    m_srv.async_run(1);
  }
  catch(const std::exception &e)
  {
    std::cerr << "Could not start service: " << e.what() << std::endl;
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
    lck.unlock();
    std::string error;

    try
    {
      FileCopier().moveFile(
        ftm.tmp.c_str(), ftm.result.fileName.c_str(), &ftm.atime);
      m_db.addEncodedFile(ftm.result);
    }
    catch(const std::exception &e)
    {
      std::cerr << e.what() << '\n';
      error = e.what();
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
    return 1;
  });

  m_srv.bind(RpcFunctions::authenticate,
    [&](const string &token) -> void { this->authenticate(token); });

  m_srv.bind(RpcFunctions::getNextFile,
    [&](const MediaFileRequirements &filter,
      MediaEncoderSettings &settings) -> MediaEncoderSettings {
      try
      {
        auto &cli = checkClient();
        getNextFile(cli, filter, settings);
      }
      catch(const std::exception &)
      {
        rpc::this_handler().respond_error(string("Forbidden"));
      }

      return settings;
    });

  m_srv.bind(
    RpcFunctions::postFile, [&](const EncodingResultInfo &result) -> void {
      this->postFile(result);
    });

  m_srv.bind(RpcFunctions::writeChunk, [&](const DataChunk &chunk) -> bool {
    return this->writeChunk(chunk);
  });

  m_srv.bind(RpcFunctions::readChunk, [&]() -> tuple<bool, DataChunk> {
    DataChunk chunk(m_cfg.chunkSize);
    bool ok = this->readChunk(chunk);
    if(!ok)
    {
      rpc::this_handler().respond_error(std::string("I/O error"));
    }
    return make_tuple(chunk.size() == m_cfg.chunkSize, std::move(chunk));
  });
}

MediaArchiverDaemon::~MediaArchiverDaemon() {}

template<>
bool MediaArchiverConfig<DaemonConfig>::parse(
  const std::string &key, const std::string &value, DaemonConfig &config)
{
  std::locale::global(std::locale("en_US.utf8"));
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
  size_t size[2];

  if(!dst.empty())
  {
    size[1] = FileCopier().getFileSize(dst.c_str());
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
    aName.erase(dst.find_last_of(m_cfg.resultFileSuffix),
      m_cfg.resultFileSuffix.length());
  }

  if(isInterestingFile(dst))
  {
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
  bool found = false;

  for(auto &conn: m_connections)
  {
    if(token == conn.second.token)
    {
      cl = std::move(conn.second);
      found = true;
      m_connections.erase(conn.first);
      continue;
    }

    if(m_connections.count(id) > 0)
    {
      if(token != conn.second.token)
      {
        m_connections.erase(conn.first);
        continue;
      }
    }
  }

  cl.lastActivify = std::chrono::steady_clock::now();
  if(!found)
  {
    cl.token = token;
  }
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
bool MediaArchiverDaemon::getNextFile(ConnectedClient &cli,
  const MediaFileRequirements &filter, MediaEncoderSettings &settings)
{
  BasicFileInfo fi;
  if(cli.inFile.is_open())
  {
    throw std::runtime_error("A file is already open for reading");
  }
  if(cli.outFile.is_open())
  {
    throw std::runtime_error("An output file is already open for writing");
  }
  cli.originalFileId = 0;
  cli.filter = filter;
  uint32_t srcId = 0;

  if(!m_stopRequested)
  {
    srcId = m_db.getNextFile(cli.filter, fi);
    if(srcId > 0)
    {
      FileCopier().getFileTimes(fi.fileName.c_str(), cli.times);
      cli.inFile.open(fi.fileName, ios_base::openmode::_S_in);
      if(cli.inFile.fail())
      {
        throw IOError(string("Could not open file: ") + fi.fileName);
      }
      cli.encSettings.fileLength = fi.fileSize;
      cli.originalFileName = fi.fileName;
      stringstream ss;
      ss << "-preset veryfast -c:v " << m_cfg.vCodec << " -c:a "
         << m_cfg.aCodec << " -crf " << m_cfg.crf << " -b:a "
         << m_cfg.aBitRate;
      cli.encSettings.commandLineParameters = ss.str();
    }
  }
  else
  {
    cli.encSettings.fileLength = 0;
    cli.originalFileName.clear();
  }

  cli.originalFileId = srcId;
  cli.encSettings.encoderType = filter.encoderType;

  auto posExt = cli.originalFileName.find_last_of('.', 0);
  cli.encSettings.fileExtension = cli.originalFileName.substr(posExt + 1);
  settings = cli.encSettings;
  // todo: fill other members
  return srcId > 0;
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
    return !cli.inFile.fail() || cli.inFile.eof();
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
    cli.inFile.close();
  }

  if(cli.outFile.is_open())
  {
    throw std::runtime_error("Invalid state");
  }

  cli.encResult = result;
  if(result.result == EncodingResultInfo::EncodingResult::OK &&
    result.fileLength > 0)
  {
    // prepare for receiving data
    std::stringstream ss;
    if(m_cfg.tempFolder == ".")
    {
      ss << cli.originalFileName << "." << cli.originalFileId;
    }
    else if(m_cfg.tempFolder.empty())
    {
      ss << "./" << cli.originalFileId;
    }
    {
      ss << m_cfg.tempFolder << '/' << cli.originalFileId;
    }

    cli.tempFileName = ss.str();
    cli.outFile.open(cli.tempFileName,
      std::ios_base::openmode::_S_bin | std::ios_base::openmode::_S_out);
  }
  else
  {
    // error during encoding, no data will be received
    std::lock_guard<std::mutex> lck(m_mtxFileMove);
    prepareNewSession(cli);
    m_cv.notify_all();
  }
}
bool MediaArchiverDaemon::writeChunk(const std::vector<char> &data)
{
  auto &cli = checkClient();
  cli.outFile.write(data.data(), data.size());
  if(cli.outFile.tellp() < cli.encResult.fileLength)
  {
    // copying not finished yet
    return true;
  }
  {
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

  // preparing the next file transfer
  cli.originalFileId = 0;
  cli.tempFileName = "";
  cli.originalFileName = "";
  cli.encSettings = MediaEncoderSettings();
  cli.encResult = EncodingResultInfo();
  cli.inFile.clear();
  cli.outFile.clear();
}

ConnectedClient &MediaArchiverDaemon::checkClient()
{
  auto &cli = m_connections.at(rpc::this_session().id());
  return cli;
}
}

int main(int argc, char **argv)
{
  // std::locale locale(std::locale(""), new no_grouping);
  // std::locale::global(locale);
  // std::setlocale(LC_NUMERIC, "C");
  // stringstream ss_;
  // ss_ << 66677788;
  // std::cout << 12345678u << ": " << std::locale().name() << "--"
  //           << ss_.str() << endl;

  std::string cfgFileName = "MediaArchiver.cfg";
  int c;
  bool showHelp = false;
  bool doFork = true;
  int verbosity = 1;
  while((c = getopt(argc, argv, "c:nvh")) != -1)
  {
    switch(c)
    {
      case 'c': cfgFileName = optarg; break;
      case 'n': doFork = false; break;
      case 'v': verbosity++; break;
      case 'h': showHelp = true; break;

      default: break;
    }
  }

  if(showHelp)
  {
    cout
      << "Usage: " << argv[0] << " [-nvh] [-c configFile]" << endl
      << "\t-n\t\tno fork, process remains in foreground" << endl
      << "\t-v\t\tincrease verbosity" << endl
      << "\t-h\t\tshow this help" << endl
      << "\t-c\t\tconfig file to use (by default MediaArchiver.cfg if used in local folder"
      << endl;
    return -1;
  }

  MediaArchiver::MediaArchiverConfig<MediaArchiver::DaemonConfig> mac(gCfg);
  auto readCfg = mac.read(cfgFileName);

  if(!readCfg)
  {
    std::cerr << "Could not read config file" << errno << std::endl;
    return 1;
  }

  std::list<std::string> folders;
  char pathSeparator = ':';
#ifdef WIN32
  pathSeparator = ';';
#endif
  std::istringstream iss(gCfg.foldersToWatch);

  for(std::string buf; std::getline(iss, buf, pathSeparator);)
  { folders.emplace_back(move(buf)); }

  if(folders.size() < 1)
  {
    std::cerr << "Missing media folder(s)" << std::endl;
    return 1;
  }

  if(false && doFork)
  {
    switch(fork())
    {
      case -1:
        std::cerr << "Could not fork: " << errno << std::endl;
        return 2;
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

  fsw.reset();
  gima.reset();
  return 0;
}
