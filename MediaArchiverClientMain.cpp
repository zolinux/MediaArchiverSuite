#include <signal.h>

#include "MediaArchiverConfig.hpp"
#include "MediaArchiverClientConfig.hpp"
#include "MediaArchiverClient.hpp"

#include "loguru.hpp"

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
  LOG_F(ERROR, "Signal caught: %i", signum);
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

int main(int argc, char **argv)
{
  loguru::g_internal_verbosity = 1;
  loguru::Options opts;
  opts.signals.sigint = false;

  loguru::init(argc, argv, opts);

  // load configuration
  {
    auto mac =
      MediaArchiver::MediaArchiverConfig<MediaArchiver::ClientConfig>(gCfg);
    try
    {
      mac.read("MediaArchiver.cfg");
    }
    catch(const std::exception &e)
    {
      LOG_F(ERROR, e.what());
      return 1;
    }
  }

  loguru::add_file("MediaArchiverClient.log", loguru::FileMode::Append,
    static_cast<loguru::Verbosity>(gCfg.verbosity));

  // Register signal and signal handler
  signal(SIGINT, signal_callback_handler);

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
