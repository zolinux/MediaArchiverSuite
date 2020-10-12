#include "SQLite.hpp"

namespace MediaArchiver
{
SQLite::SQLite()
  : m_db(nullptr)
{
}
SQLite::~SQLite()
{
  if(m_db)
  {
    sqlite3_close(m_db);
    m_db = nullptr;
  }
}
void SQLite::init() {}
void SQLite::connect(const char *connectionString, bool create)
{
  if(m_db)
    throw DBError(
      DBError::DBErrorCodes::ConnectionError, "Database already opened");

  // ToDo: create folder to DB file if not exists
  auto ret = sqlite3_open(connectionString, &m_db);
  if(ret != SQLITE_OK)
  {
    throw DBError(
      DBError::DBErrorCodes::ConnectionError, sqlite3_errmsg(m_db));
  }

  // ToDo check & create
  if(!isDBInitialized())
  {
    if(create)
    {
      setupTables();
    }
    else
      throw DBError(DBError::DBErrorCodes::EmptyDatabase,
        "Database is not initialized");
  }
}

int SQLite::sqliteCallbackInvoker(
  void *ptr, int argc, char **fields, char **names)
{
  return (*static_cast<Sqlite3CallbackFunctor *>(ptr))(
    ptr, argc, fields, names);
}
bool SQLite::isDBInitialized() const
{
  int tables = 0;

  auto cb = unique_ptr<Sqlite3CallbackFunctor>(new Sqlite3CallbackFunctor(
    [&tables](void *ptr, int argc, char **fields, char **names) {
      if(argc != 1)
      {
        throw DBError(DBError::DBErrorCodes::InvalidDataReturned,
          "number of returned columns too high");
      }

      constexpr const char *tableNames[] = {
        "sourcefiles", "archives", "queue"};

      for(const auto &tbl: tableNames)
      {
        if(strcmp(tbl, fields[0]) == 0)
        {
          tables++;
          break;
        }
      }
      return 0;
    }));

  SQL
    << cb
    << "SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%'";
  return tables == 3;
}
void SQLite::setupTables()
{
  SQL
    << "BEGIN TRANSACTION;"
       "CREATE TABLE sourcefiles (id INTEGER PRIMARY KEY AUTOINCREMENT, path TEXT, size INTEGER);"
       "CREATE TABLE archives (id INTEGER, path TEXT);"
       "CREATE TABLE queue (id INTEGER, status INTEGER, count INTEGER, start timestamp, comment TEXT);"
       "COMMIT;";
}
void SQLite::execSql(const char *sql, sqlite3_callback cb, void *data) const
{
  char *zErrMsg = nullptr;
  auto rc = sqlite3_exec(m_db, sql, cb, data, &zErrMsg);

  if(rc != SQLITE_OK && string("query aborted") != zErrMsg)
  {
    auto s = string("SQL Error: ") + zErrMsg;
    sqlite3_free(zErrMsg);
    throw DBError(DBError::DBErrorCodes::SqlError, s);
  }
}
void SQLite::disconnect()
{
  checkDBOpened();
}
uint32_t SQLite::getNextFile(
  const MediaFileRequirements &filter, BasicFileInfo &file)
{
  lock_guard<mutex> lck(m_mtx);
  checkDBOpened();

  auto maxSize = filter.maxFileSize > 0 ?
    filter.maxFileSize :
    std::numeric_limits<size_t>().max();

  uint32_t srcId = 0;

  auto cb = unique_ptr<Sqlite3CallbackFunctor>(new Sqlite3CallbackFunctor(
    [&](void *ptr, int argc, char **fields, char **names) {
      srcId = atoi(fields[0]);
      file.fileName = fields[1];
      file.fileSize = atol(fields[2]);
      return 1; // abort after 1st record
    }));

  SQL
    << cb
    << "select sourcefiles.id, path, size from sourcefiles join queue on queue.id=sourcefiles.id where sourcefiles.size <= "
    << maxSize
    << " and queue.status=0 union select sourcefiles.id, path, size from sourcefiles where sourcefiles.size <= "
    << maxSize
    << " and sourcefiles.id not in (select id from queue) union select sourcefiles.id, path, size from sourcefiles join queue on queue.id=sourcefiles.id where sourcefiles.size <= "
    << maxSize
    << " and queue.status<0 and queue.status >=-99 and queue.count < 3 LIMIT 1";

  if(srcId == 0)
  {
    // no file has been found
    file.fileSize = 0;
    file.fileName = "";
    return 0;
  }

  // update queue/status
  int count = 0;
  bool queued = false;

  cb.reset(new Sqlite3CallbackFunctor(
    [&](void *ptr, int argc, char **fields, char **names) {
      count = atoi(fields[0]);
      queued = true;
      return 0;
    }));

  // INSERT INTO queue VALUES(1930,5,0,'2019-11-22 20:46:05.336946',NULL);
  SQL << cb << "select count from queue where queue.id=" << srcId;
  cb.reset();

  if(!queued)
  {
    SQL << "insert into queue (id,status,count,start) VALUES (" << srcId
        << ",1,1," << ExecSQL::now << ")";
  }
  else
  {
    SQL << "update queue set status=1,count=count+1,start=" << ExecSQL::now
        << " where queue.id=" << srcId;
  }
  return srcId;
}

uint32_t SQLite::addFile(
  const BasicFileInfo *src, const BasicFileInfo *dst, bool queue)
{
  lock_guard<mutex> lck(m_mtx);
  checkDBOpened();

  bool srcGiven = src != nullptr && !src->fileName.empty();
  bool dstGiven = dst != nullptr && !dst->fileName.empty();

  uint32_t srcId = 0;
  bool inQueue = false;
  bool processed = false;
  bool putToQueue = false;
  string archiveName;
  auto cb = unique_ptr<Sqlite3CallbackFunctor>();

  // check if file exists
  if(srcGiven)
  {
    cb.reset(new Sqlite3CallbackFunctor(
      [&](void *ptr, int argc, char **fields, char **names) {
        srcId = fields[0] != NULL ? atoi(fields[0]) : 0;
        inQueue = fields[1] != NULL && strlen(fields[1]);
        processed = inQueue ? atoi(fields[1]) >= 5 : false;
        if(fields[2] != NULL && strlen(fields[2]))
          archiveName = fields[2];
        return 0;
      }));

    SQL
      << cb
      << "select sourcefiles.id,queue.status,archives.path from sourcefiles left join queue using (id) left join archives using(id) where sourcefiles.path='"
      << src->fileName << "'";

    if(!srcId)
    {
      // put file into sources

      SQL << "INSERT INTO sourcefiles (path,size) VALUES('" << src->fileName
          << "'," << src->fileSize << ")";

      srcId = sqlite3_last_insert_rowid(m_db);
    }

    if(!inQueue && (queue || dstGiven))
    {
      // we should put the file in queue
      SQL << "INSERT INTO queue (id,status,count,start) VALUES(" << srcId
          << "," << (dstGiven ? 5 : 0) << ",0," << ExecSQL::now << ")";

      putToQueue = true;
      inQueue = true;
    }
  }

  if(dstGiven)
  {
    bool found = false;
    uint32_t srcId2 = srcId;

    if(!archiveName.empty())
    {
      if(dst->fileName.compare(archiveName) != 0)
      {
        cerr << "Destination file is already present with another name: <"
             << dst->fileName << "> != <" << archiveName << ">" << endl;
      }
      else
      {
        // archive name already found at putting source
        return srcId;
      }
    }

    // try to look up dst in archives and get the srcId
    cb.reset(new Sqlite3CallbackFunctor(
      [&](void *ptr, int argc, char **fields, char **names) {
        found = true;
        srcId = fields[0] != NULL ? atoi(fields[0]) : 0;
        inQueue = fields[1] != NULL && strlen(fields[1]);
        putToQueue = inQueue ? atoi(fields[1]) >= 5 : false;
        return 0;
      }));

    SQL
      << cb
      << "select archives.id,queue.status from archives left join queue using (id) where archives.path='"
      << dst->fileName << "'";

    if(!archiveName.empty() && srcId != srcId2)
    {
      cerr << "different Ids for encoded file: " << srcId << "!=" << srcId2
           << " file: " << dst->fileName << endl;
    }

    // if archive not present, add it w/ srcId, if present just add to queue
    // if needed
    if(!found)
    {
      // add to archives an archives
      SQL << "INSERT INTO archives (id,path) VALUES (" << srcId << ",'"
          << dst->fileName << "');";
    }

    if(srcId)
    {
      if(!inQueue)
      {
        SQL << "INSERT INTO queue (id,status,count,start) VALUES(" << srcId
            << ",5,0," << ExecSQL::now << ")";
      }
      else if(!putToQueue)
      {
        SQL << "update queue set status=5,start=" << ExecSQL::now
            << ",comment=NULL where id=" << srcId;
      }
    }
  }

  return srcId;
}
void SQLite::addEncodedFile(const EncodedFile &file)
{
  lock_guard<mutex> lck(m_mtx);
  checkDBOpened();

  string comment("NULL");

  if(!file.error.empty())
    comment = string("'") + file.error + "'";

  SQL << "update queue set status=" << file.result
      << ",start=" << ExecSQL::now << ",comment=" << comment
      << " where id=" << file.originalFileId << ";"
      << "INSERT INTO archives (id,path) VALUES (" << file.originalFileId
      << ",'" << file.fileName << "')";
}

void SQLite::checkDBOpened() const
{
  if(!m_db)
    throw DBError(
      DBError::DBErrorCodes::ConnectionError, "Database not opened");
}
}