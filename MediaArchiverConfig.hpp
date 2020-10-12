#ifndef __MEDIAARCHIVERCONFIG_HPP__
#define __MEDIAARCHIVERCONFIG_HPP__

#include <string>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <algorithm>

namespace MediaArchiver
{
// trim from start (in place)
static inline void ltrim(std::string &s)
{
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
    return !std::isspace(ch);
  }));
}

// trim from end (in place)
static inline void rtrim(std::string &s)
{
  s.erase(std::find_if(
            s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch); })
            .base(),
    s.end());
}

// trim from both ends (in place)
static inline void trim(std::string &s)
{
  ltrim(s);
  rtrim(s);
}

template<class T> class MediaArchiverConfig
{
public:
  MediaArchiverConfig(T &config)
    : m_config(config)
  {
  }

  bool read(const std::string &file)
  {
    bool readData = false;
    auto fs = std::ifstream(file);
    try
    {
      fs.open(file, std::ios::in);
      if(!fs.is_open())
      {
        throw std::runtime_error("File cannot be opened");
      }
      fs.clear();

      if(fs.fail())
      {
        auto err = fs.flags();
        throw std::runtime_error(
          std::string("Failure after file opened: "));
      }

      std::string line;
      while(!fs.fail())
      {
        std::getline(fs, line);
        trim(line);
        auto iComment = line.find('#');

        if(iComment != std::string::npos)
        {
          if(iComment < 1)
            continue;
          line.resize(iComment);
        }

        auto iSep = line.find('=');

        if(iSep == std::string::npos)
          continue;

        auto key = line.substr(0, iSep);
        trim(key);
        auto value = line.substr(iSep + 1);
        trim(value);

        readData = true;
        if(!parse(key, value, m_config))
        {
          std::stringstream ss;
          ss << "could not parse key \"" << key << "\"";
          throw std::invalid_argument(ss.str());
        }
      }
    }
    catch(std::exception &ex)
    {
    }
    if(fs.is_open())
      fs.close();
    return readData;
  }

private:
  T &m_config;

  bool parse(const std::string &key, const std::string &value, T &config);
};
}

#endif