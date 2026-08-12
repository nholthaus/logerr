# LOGERR

A simple, portable, logging and error handling system. Logerr features:

- lightweight
- easily integrated or removed
- simple to use
- natively thread safe
- highly performant

## Build

Logerr requires CMake 3.25 or newer and a C++23 compiler. On Linux, stack traces also require the binutils development
libraries (`binutils-dev` and `libiberty-dev` on Debian/Ubuntu).

Logerr comes in two varieties: a standard C++ library and a Qt 6 integration library.

### Build Vanilla C++

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Build with Qt

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_WITH_QT=ON
cmake --build build --parallel
```

Qt is found with CMake's standard package discovery. If it is not already on CMake's prefix path, pass either
`-DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/<compiler>` or `-DQt6_DIR=/path/to/Qt/6.x/<compiler>/lib/cmake/Qt6`; no environment
variable is required.

To build and run the complete GoogleTest suite, add `-DBUILD_TESTING=ON`, then run:

```bash
ctest --test-dir build --output-on-failure
```

For multi-config generators such as Visual Studio, also pass `--config Release` when building and
`--build-config Release` to CTest.

### Adding to your project

The easiest way to incorporate `logerr` is to add it to your project as a subdirectory (or submodule), then link to
`logerr::logerr` or `logerr::qlogerr`:

```cmake
add_subdirectory(path/to/logerr)
target_link_libraries(my_target PRIVATE logerr::logerr) # or logerr::qlogerr
```

Repository warning flags, implementation definitions, and Qt lookup settings are private. C++23 and the compiler/linker
options that retain stack-trace symbols intentionally propagate to consumers so stack traces remain symbolizable.

## Usage

All necessary macros for client code are defined in the `logerr` header.

### Main Function

For logerr to work, it needs to install various signal and exception handlers. The simplest way to accomplish this is by
wrapping your main function with the provided convenience macros:

#### Console Applications

```cpp
#include <logerrConsoleApplication.h>

int main()
{
	// This should be the first line in `main`
    LOGERR_CONSOLE_APP_BEGIN

    // your code here...
    // ...
    // ...

    // This should be the last line in `main`
    LOGERR_CONSOLE_APP_END
}
```

The console application macros are available when linking either to the `logerr` _or_ `qlogerr` libraries.

#### GUI Applications

```cpp
#include <logerrGuiApplication.h>

int main()
{
	// This should be the first line in `main`
    LOGERR_GUI_APP_BEGIN

    // your code here...
    // ...
    // ...

    // This should be the last line in `main`
    LOGERR_GUI_APP_END
}
```

The GUI macros are _only_ available when linking to `qlogerr`.

### Errors

`logerr` uses modified `exceptions`, called `errors`, as its primary error handling mechanism. Errors come in two flavors, fatal
and non-fatal. To generate an error, simply call the macro:

```cpp
#include <logerr>

void myFunc()
{
    if(somethingDidntWork)
        ERR("myFunc failed to do what it was supposed to do!");
    
    // otherwise...
}
```
All errors are automatically logged, along with system diagnostic information and a stack trace produced at the point of the error.

Unlike exceptions, it is safe to generate errors from threads. Uncaught errors will be passed to the main thread and re-thrown.

#### ERR vs. FATAL_ERR

- Use `ERR(...)` when you want to notify the user of a potentially non-fatal error, such as failure to open a requested file.
- Use `FATAL_ERR(...)` when the error requires termination of the program.

### Logging

`logerr` redirects stdout for its logging mechanism, so all you need to do to log is simply write to stdout as you usually would.

That said, to get the maximum benefit out of the library, it's recommended that you replace instances of `std::cout` with one of the
four convenience logging macros:
- `LOGINFO`
- `LOGDEBUG`
- `LOGWARNING`
- `LOGERR`

_Example:_
```cpp
#include <logerr>

void myFunc()
{
    LOGINFO << "myFunc() started" << std::endl;
    
    // code...
    
    LOGINFO << "myFunc() finished" << std::endl;
}
```

Logging is inherently threaded _and_ thread safe, so you can log from multiple threads at the same time with very little
performance impact to your core code.

#### ERR vs. LOGERR

`ERR` throws a `logerr::exception` carrying its source location and a stack captured at the throw site. Use it when the
current exception boundary can safely surface the failure.

`LOGERR` and `LOGWARNING` do not throw, but include `[file:line function]` automatically. They are the right choice when
the caller must preserve its return/cleanup contract, including destructors and error-isolation paths. For the rarer
non-throwing fault that warrants full symbolization, use `LOGERR_TRACE(message)`; it captures a stack at that call site
and then preserves normal control flow.

### Multicast Log Channel (Qt)

`qlogerr` can broadcast log lines over UDP multicast so a separate viewer can display them: a `LogBlaster` yeets each
line to a well-known group/port, and any number of `LogReceiver`s (e.g. a GUI app's log dock) pick them up. The channel
defaults to `239.239.239.239:52387` and is **shared** — multiple processes can listen at once.

The channel is optionally configurable via environment variables, so you can run independent logerr apps on private
channels (so they don't see each other's logs), or move off a busy port — no rebuild required:

| Variable           | Meaning                 | Default           |
|--------------------|-------------------------|-------------------|
| `LOGERR_LOG_PORT`  | UDP port (1–65535)      | `52387`           |
| `LOGERR_LOG_GROUP` | Multicast group address | `239.239.239.239` |

A blaster and the receiver(s) it feeds must use the same values. `LogBlaster`'s constructor also accepts an explicit
host/port (a null address / `0` port fall back to the channel defaults above).

### TODOs

`logerr` provides a `TODO` macro that generates messages at compile time:

```cpp
#include <logerr>

int main()
{
    TODO("Add `logerr` to this main function");	
}
```

### Best Practices

- Don't log in loops
- Don't repeat yourself
- Prefer `ERR` to `LOGERR`
- remove `DEBUG` logging when you're done with it.

### LOGERR EXIT CODES

- `0`:  success.
- `1`:  The program crashed (segfault, SIGSEV).
- `2`:  Exited due to uncaught `logerr::exception`.
- `3`:  Exited due to uncaught `std::exception`.
- `4`:  Exited due to unknown exception.
- `12`: No main thread ID set (probably missing a call to the `BEGIN` macro)
- `13`: A logerr thread error was rethrown from a non-main thread (you probably called `LOGERR_RETHROW()` from the wrong
  place).
