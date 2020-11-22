#ifndef __ServerIf_H__
#define __ServerIf_H__

#include <utility>
#include <tuple>

#include "IMediaArchiverServer.hpp"
#include "MediaArchiverClientConfig.hpp"

#include "rpc/client.h"
#include "rpc/rpc_error.h"
#include "RpcFunctions.hpp"

#include "loguru.hpp"
namespace MediaArchiver
{
class ServerIf : public IServer
{
private:
  std::unique_ptr<rpc::client> m_rpc;

public:
  ServerIf(const ClientConfig &cfg)
  {
    LOG_F(1, "Connecting to %s:%u", cfg.serverName.c_str(), cfg.serverPort);
    m_rpc.reset(new rpc::client(cfg.serverName, cfg.serverPort));
    // m_rpc->set_timeout(3600000);
    m_rpc->set_timeout(cfg.serverConnectionTimeout);
  }

  virtual void authenticate(const std::string &token) override
  {
    LOG_F(1, "Authenticating with token %s", token.c_str());
    m_rpc->call(RpcFunctions::authenticate, token);
  }

  virtual bool isConnected() const override
  {
    return m_rpc &&
      m_rpc->get_connection_state() ==
      rpc::client::connection_state::connected;
  }

  virtual void reset() override
  {
    LOG_F(1, "Resetting transmission");
    m_rpc->call(RpcFunctions::reset);
  }

  virtual void abort() override
  {
    LOG_F(1, "Aborting transmission");
    m_rpc->call(RpcFunctions::abort);
  }

  virtual bool getNextFile(const MediaFileRequirements &filter,
    MediaEncoderSettings &settings) override
  {
    LOG_F(INFO, "Requesting next file...");
    settings = m_rpc->call(RpcFunctions::getNextFile, filter, settings)
                 .as<MediaEncoderSettings>();
    LOG_F(INFO, "SRC File length: %lu", settings.fileLength);
    return settings.fileLength > 0;
  }

  virtual bool readChunk(std::ostream &file) override
  {
    std::tuple<bool, DataChunk> data =
      m_rpc->call(RpcFunctions::readChunk).as<std::tuple<bool, DataChunk>>();

    const auto &content = std::get<1>(data);
    auto len = content.size();

    file.write(content.data(), len);
    auto b = std::get<0>(data);
    LOG_IF_F(INFO, !b, "Source file reading finished");

    return b;
  }
  virtual bool readChunk(DataChunk &buffer)
  {
    try
    {
      auto response = m_rpc->call(RpcFunctions::readChunk);
      auto data = response.as<std::tuple<bool, DataChunk>>();

      const auto &content = std::get<1>(data);
      auto len = content.size();

      buffer.resize(len);
      buffer = std::move(content);
      return std::get<0>(data);
    }
    catch(rpc::rpc_error &e)
    {
      LOG_F(ERROR, "read chunk: %s", e.what());
      throw std::runtime_error(std::string("read chunk: ") + e.what());
    }
    catch(std::exception &ex)
    {
      LOG_F(ERROR, "read chunk: %s", ex.what());
      throw std::runtime_error(std::string("read chunk ERROR"));
    }
    return false;
  }

  virtual void postFile(const EncodingResultInfo &result) override
  {
    VLOG_F(result.result == EncodingResultInfo::EncodingResult::OK ? 0 : -2,
      "Signaling result (%lu): %s ", result.fileLength,
      result.error.c_str());
    m_rpc->call(RpcFunctions::postFile, result);
  }

  virtual bool writeChunk(const DataChunk &data) override
  {
    try
    {
      auto response = m_rpc->call(RpcFunctions::writeChunk, data);
      auto res = response.as<bool>();
      return res;
    }
    catch(rpc::rpc_error &e)
    {
      LOG_F(ERROR, "Writing chunk: %s", e.what());
      throw std::runtime_error(std::string("write chunk: ") + e.what());
    }
    return false;
  }

  virtual uint32_t getVersion() const override
  {
    ERROR_CONTEXT("Inquiring server version failed", 0);
    auto v = m_rpc->call(RpcFunctions::getVersion).as<uint32_t>();
    return v;
  }

  virtual ~ServerIf()
  {
    LOG_F(1, "Disconnecting from server...");
    m_rpc.reset();
  }
};
}
#endif
