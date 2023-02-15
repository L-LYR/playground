driver_deps = []

driver_deps += dependency('fmt')
driver_deps += dependency('spdlog')
driver_deps += dependency('spdk_nvme')
driver_deps += dependency('spdk_env_dpdk')
driver_deps += dependency('boost', version: '=1.80.0')

libdriver = library(
    'driver',
    'driver.cc',
    dependencies: driver_deps,
)

executable(
    'test_driver',
    'test_driver.cc',
    dependencies: driver_deps,
    link_with: [libdriver, libspdk_env],
)
executable(
    'test_attach',
    'test_attach.cc',
    dependencies: driver_deps,
    link_with: [libdriver, libspdk_env],
)

executable(
    'try',
    'try.cc',
    dependencies: driver_deps,
)
