argobots_play_deps = []

argobots_play_deps += dependency('argobots') # need --enable-tool
argobots_play_deps += dependency('threads')

executable('hello_world', 'hello_world.c', dependencies: argobots_play_deps)
executable('hello_world_ws', 'hello_world_ws.c', dependencies: argobots_play_deps)
executable('async_engine', 'async_engine.c', dependencies: argobots_play_deps)
