project(
    'playground',
    'c',
    'cpp',
    version: '0.1',
    default_options: [
        'warning_level=1',
        'c_std=c11',
        'cpp_std=c++20',
    ],
)

add_project_arguments(
    '-Wno-unused-parameter',
    # '-fcoroutines-ts',
    language: ['c', 'cpp'],
)

common_include_directories = include_directories('include')

subdir('common')
subdir('argobots_play')
subdir('driver')
