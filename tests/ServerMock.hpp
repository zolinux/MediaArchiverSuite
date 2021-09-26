#include "MediaArchiverClientConfig.hpp"
#include "IMediaArchiverServer.hpp"

#include <vector>
#include <string>
#include <fstream>

namespace MediaArchiver
{
class ServerMock : public IServer
{
public:
  explicit ServerMock(const ClientConfig &config,
    const std::vector<std::string> &files)
    : m_config(config)
    , m_files(files)
  {
  }

  uint32_t getVersion() const override { return 1; }
  bool isConnected() const override { return true; }

  void authenticate(const std::string &token) override {}
  void reset() override {}
  void abort() override {}
  bool getNextFile(const MediaFileRequirements &filter,
    MediaEncoderSettings &settings) override;
  bool readChunk(std::ostream &file) override;
  void postFile(const EncodingResultInfo &result) override;
  bool writeChunk(const std::vector<char> &data) override { return true; }
  ~ServerMock() = default;

private:
  const ClientConfig &m_config;
  const std::vector<std::string> &m_files;
  std::ifstream m_stream;
  size_t m_fileSize = 0U;
  size_t m_rxPos = 0U;
  size_t m_txPos = 0U;
};
}