rm -rf build
mkdir -p build && cd build
cmake -DHEARTOS_PORT=null -DHEARTOS_BUILD_EXAMPLES=ON ..
cmake --build . -j$(nproc)
./examples/two_tasks/two_tasks
