# Cross Compile

To compile the library for a different architecture use the following instructions:

## ARM BAREMETAL

### Pre-requisites

install gcc by version 

```bash
# commands go here

sudo apt install ...
 
```

### Build

use the following command to build the project
```bash

cmake -S . -B build-mcu \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi.cmake \
  -DHEARTOS_PORT=cortex_m \
  -DHEARTOS_BUILD_EXAMPLES=OFF
  
make ...

```

> **NOTE:** This example builds for `cortex-m7`, if you are building for a different processor update it accordingly. 

### include in your project as follows:
#### CMake

#### Others?