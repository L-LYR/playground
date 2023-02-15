#include "spdk_env.hh"

namespace common {

spdk_env_opts *SPDKEnv::opts = nullptr;

auto SPDKEnv::Initialized() -> bool { return opts != nullptr; }

SPDKEnv::SPDKEnv() {
  if (not Initialized()) {
    printf("[Hi, there] SPDK Env is ready!\n");
    opts = new spdk_env_opts;
    spdk_env_opts_init(opts);
    opts->name = "spdk_env";
    if (auto rc = spdk_env_init(opts); rc != 0) {
      exit(rc);
    }
  }
}

SPDKEnv::~SPDKEnv() {
  if (Initialized()) {
    printf("[Hi, there] SPDK Env is down!\n");
    spdk_env_fini();
    delete opts;
    opts = nullptr;
  }
}

}  // namespace common