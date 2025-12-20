rm -rf build
mkdir -p build && cd build
cmake -DHARDRT_PORT=null -DHARDRT_BUILD_EXAMPLES=ON ..
cmake --build . -j$(nproc)
./examples/two_tasks/two_tasks
