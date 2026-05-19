# Orbwvr

Orbwvr is an asynchronous C++20 PostgreSQL driver. It provides a non-blocking interface to PostgreSQL by combining the official `libpq` C library with standalone Asio and C++20 coroutines.

## Architecture

Orbwvr is designed to prevent database I/O from blocking the execution thread. It achieves this by wrapping `libpq` asynchronous functions and polling the socket file descriptor using an Asio I/O context. C++20 coroutines are used to provide a linear, readable API over the asynchronous state machine.

## Requirements

To build and use Orbwvr, the following are required:
* A C++20 compliant compiler with coroutine support.
* CMake 3.20 or higher.
* PostgreSQL development headers and libraries (`libpq`).
* Threads availability on the host system.

*Note: Asio is handled automatically via CMake FetchContent and does not require manual installation.*

## Integration

The recommended way to integrate Orbwvr into a project is via CMake `FetchContent`. Add the following to your `CMakeLists.txt`:

```cmake
include(FetchContent)
FetchContent_Declare(
    orbwvr
    GIT_REPOSITORY https://github.com/creedpetitt/orbwvr
    GIT_TAG main
)
FetchContent_MakeAvailable(orbwvr)

target_link_libraries(your_target_name PRIVATE orbwvr)
```

## Usage

Orbwvr exposes an asynchronous API that must be awaited within a coroutine context. Simply include the `"orbwvr.h"` header to use the library.   

Because the C++ standard forbids the `main()` function from being a coroutine, Orbwvr provides a `sync_wait` utility. This function blocks the calling thread until the provided coroutine completes, acting as the bridge between your synchronous entry point and your asynchronous database logic.

Here is a basic example (from `examples/basic.cpp`):

```cpp
#include "orbwvr.h"

#include <exception>
#include <iostream>
#include <vector>

orbwvr::async<void> run_database_test() {
    try {

        // PG connection string- automatically connects on construction
        orbwvr::database db("postgresql://postgres:DBNAME@localhost:PORT/postgres");

        // Vector holding paramater
        std::vector<std::string> params{"1"};

        // Simple query
        co_await db.query("SELECT 1");
        std::cout << "SUCCESSFULLY QUERIED DB" << std::endl;

        // Query using parameters 
        co_await db.query_params("SELECT $1::int", params);
        std::cout << "SUCCESSFULLY QUERIED DB WITH PARAMS" << std::endl;

        // Simple prepare statement
        co_await db.prepare("s1", "SELECT $1::int");
        std::cout << "SUCCESSFULLY PREPARED STATEMENT" << std::endl;

        // Query using prepared statement
        co_await db.query_prepared("s1", params);
        std::cout << "SUCCESSFULLY QUERIED PREPARED STATEMENT" << std::endl;

    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
    }
    co_return;
}

int main() {
    try {
        orbwvr::sync_wait(run_database_test());
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
    }
}
```
