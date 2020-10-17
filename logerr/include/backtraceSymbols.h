// attribution: https://oroboro.com/printing-stack-traces-file-line/

#ifdef Q_OS_WIN
#error "this file should only be used on linux"
#endif

#include <string>
#include <utility>
#include <vector>

/// @brief Backtrace symbols returning file and function names
/// @param addrList The output of the linux `backtrace` function
/// @param numAddr The size returned by the linux `backtrace` function
/// @return a vector of string pairs, where the first is the filename:line, and the second is the function name
std::vector<std::pair<std::string, std::string>>        backtraceSymbols(void* const* addrList, int numAddr);
