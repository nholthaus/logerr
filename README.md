# LOGERR

A simple, portable, logging and error handling system. Logerr features:

- lightweight
- easily integrated or removed
- simple to use
- natively thread safe
- highly performant

## Build

Logerr comes in two varieties: a vanilla c++ version, and a Qt compatible version

### Build Vanilla C++

``` bash
cd <logerr root dir>
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -- -j4
```

### Build with Qt

``` bash
cd <logerr root dir>
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_WITH_QT=On ..
cmake --build . -- -j4
```

### Adding to your project

The easiest way to incorporate `logerr` is to add it to your project as a subdirectory (or submodule), and then link to
the
`logerr` or `qlogerr` library, depending on which flavor you built.

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

Errors are automatically logged, and generate substantially more information that the `LOGERR` call. So when should you use `LOGERR`?

Sparingly. In general, always prefer `ERR` to `LOGERR`, _except_ in situations where exceptions cannot be thrown, e.g. inside a destructor.

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