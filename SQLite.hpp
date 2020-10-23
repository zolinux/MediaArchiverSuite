#ifndef __SQLITE_HPP__
#define __SQLITE_HPP__

#include <stdlib.h>
#include <string>
#include <sstream>
#include <memory>
#include <sqlite3.h>
#include <mutex>
#include <functional>
#include <chrono>
#include <iomanip>
#include <iostream>

#include "IDatabase.hpp"
using namespace std;

namespace MediaArchiver
{
class ExecSQL;

class SQLite : public IDatabase
{
public:
  SQLite();
  virtual void init() override;
  virtual void connect(const char *connectionString, bool create) override;
  virtual void disconnect() override;
  virtual uint32_t getNextFile(
    const MediaFileRequirements &filter, BasicFileInfo &file) override;
  virtual uint32_t addFile(const BasicFileInfo *src,
    const BasicFileInfo *dst, bool queue) override;
  virtual void addEncodedFile(const EncodedFile &file) override;
  virtual void reset(uint32_t srcFileId) override;
  virtual ~SQLite();

  using Sqlite3CallbackFunctor =
    std::function<int(void *, int, char **, char **)>;

private:
  friend class ExecSQL;
  static int sqliteCallbackInvoker(void *, int, char **, char **);
  void checkDBOpened() const;
  bool isDBInitialized() const;
  void setupTables();
  void execSql(const char *sql, sqlite3_callback cb = nullptr,
    void *data = nullptr) const;
  sqlite3 *m_db;
  mutex m_mtx;
};

#define SQL ExecSQL(this)

class ExecSQL
{
private:
  const SQLite *m_sqlite;
  SQLite::Sqlite3CallbackFunctor *m_cb;
  std::stringstream m_ss;
  char m_buf[32];

public:
  ExecSQL(const SQLite *sqlite)
    : m_sqlite(sqlite)
    , m_cb(nullptr)
  {
  }

  static struct NOW
  {
  } now;

  ExecSQL &operator<<(std::unique_ptr<SQLite::Sqlite3CallbackFunctor> &cb)
  {
    m_cb = cb.get();
    return *this;
  }
  ExecSQL &operator<<(const std::string &s)
  {
    m_ss << s;
    return *this;
  }
  ExecSQL &operator<<(const char *s)
  {
    m_ss << s;
    return *this;
  }
  ExecSQL &operator<<(int i)
  {
    snprintf(m_buf, sizeof(m_buf), "%i", i);
    m_ss << m_buf;
    return *this;
  }
  ExecSQL &operator<<(uint32_t i)
  {
    snprintf(m_buf, sizeof(m_buf), "%u", i);
    m_ss << m_buf;
    return *this;
  }
  ExecSQL &operator<<(size_t i)
  {
    snprintf(m_buf, sizeof(m_buf), "%lu", i);
    m_ss << m_buf;
    return *this;
  }
  ExecSQL &operator<<(std::_Put_time<char> i)
  {
    m_ss << "'" << i << "'";
    return *this;
  }
  ExecSQL &operator<<(const NOW now)
  {
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);

    auto dt = put_time(&tm, "%F %T");
    m_ss << "'" << dt << "'";
    return *this;
  }
  ~ExecSQL()
  {
    m_sqlite->execSql(m_ss.str().c_str(),
      m_cb ? &SQLite::sqliteCallbackInvoker : nullptr, m_cb);
  }
};
}
#endif