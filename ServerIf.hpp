#ifndef __ServerIf_H__
#define __ServerIf_H__

#include <utility>
#include <tuple>

#include "IMediaArchiverServer.hpp"
#include "MediaArchiverClientConfig.hpp"

#include "rpc/client.h"
#include "RpcFunctions.hpp"

namespace MediaArchiver
{
class ServerIf : public IServer
{
private:
  std::unique_ptr<rpc::client> m_rpc;

public:
  ServerIf(const ClientConfig &cfg)
  {
    m_rpc.reset(new rpc::client(cfg.serverName, cfg.serverPort));
    // m_rpc->set_timeout(3600000);
    m_rpc->set_timeout(cfg.serverConnectionTimeout);
  }

  virtual void authenticate(const std::string &token) override
  {
    m_rpc->call(RpcFunctions::authenticate, token);
  }
  virtual bool isConnected() const override
  {
    return m_rpc &&
      m_rpc->get_connection_state() ==
      rpc::client::connection_state::connected;
  }

  virtual void reset() override { m_rpc->call(RpcFunctions::reset); }
  virtual bool getNextFile(const MediaFileRequirements &filter,
    MediaEncoderSettings &settings) override
  {
    settings = m_rpc->call(RpcFunctions::getNextFile, filter, settings)
                 .as<MediaEncoderSettings>();
    return settings.fileLength > 0;
  }
  virtual bool readChunk(std::ostream &file) override
  {
    std::tuple<bool, DataChunk> data =
      m_rpc->call(RpcFunctions::readChunk).as<std::tuple<bool, DataChunk>>();

    const auto &content = std::get<1>(data);
    auto len = content.size();

    file.write(content.data(), len);
    return std::get<0>(data);
  }
  virtual void postFile(const EncodingResultInfo &result) override
  {
    m_rpc->call(RpcFunctions::postFile, result);
  }
  virtual bool writeChunk(const std::vector<char> &data) override
  {
    auto res = m_rpc->call(RpcFunctions::writeChunk, data).as<bool>();
    return res;
  }
  virtual uint32_t getVersion() const override
  {
    return m_rpc->call(RpcFunctions::getVersion).as<uint32_t>();
  }

  virtual ~ServerIf() { m_rpc.reset(); }
};

}
#endif