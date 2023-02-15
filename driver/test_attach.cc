#include "../common/spdk_env.hh"
#include "driver.hh"

using namespace driver;

common::SPDKEnv _env_;

auto main(int argc, char* argv[]) -> int {
  DevConfig config{
      .is_rdma = true,
      .is_ipv4 = true,
      .ns_id = 1,
      .addr = "192.168.200.17",
      .svc_id = "4422",
  };
  boost::asio::io_context ctx(1);

  for (uint32_t i = 0; i < 4; i++) {
    config.svc_id = std::to_string(4420 + i);
    Dev dev(ctx);
    dev.Attach(config);
    dev.Detach();
  }
  return 0;
}