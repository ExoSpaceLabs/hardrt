rm -rf build
mkdir build && cd build
cmake -DHEARTOS_PORT=posix -DHEARTOS_BUILD_EXAMPLES=ON ..
cmake --build . -j$(nproc)
./examples/two_tasks/two_tasks