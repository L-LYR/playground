#pragma once

#include <spdk/env.h>

namespace common {

class SPDKEnv {
  static spdk_env_opts *opts;

 public:
  static auto Initialized() -> bool;

 public:
  SPDKEnv(SPDKEnv &) = delete;
  SPDKEnv(SPDKEnv &&) = delete;
  auto operator=(SPDKEnv &) -> SPDKEnv = delete;
  auto operator=(SPDKEnv &&) -> SPDKEnv = delete;

 public:
  SPDKEnv();
  ~SPDKEnv();
};

}  // namespace common