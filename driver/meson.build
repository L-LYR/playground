driver_deps = []

driver_deps += dependency('fmt')
driver_deps += dependency('spdlog')
driver_deps += dependency('spdk_nvme')
driver_deps += dependency('boost', version: '=1.80.0')

library(
    'driver',
    'driver.cc',
    dependencies: driver_deps,
    include_directories: common_include_directories,
)

executable(
    'try',
    'try.cc',
    dependencies: driver_deps,
    include_directories: common_include_directories,
)
