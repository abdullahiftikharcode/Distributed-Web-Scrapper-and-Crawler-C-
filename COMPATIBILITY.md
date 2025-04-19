# Distributed Web Scraper Compatibility Guide

This document explains the compatibility features in the distributed web scraper that allow it to work with older C++ compilers and libraries.

## Compatibility Issues

The distributed web scraper uses several C++11 features, including:
- Threads and threading primitives (`std::thread`, `std::mutex`, etc.)
- Atomic operations (`std::atomic`)
- Lambda functions
- Modern networking functions (`inet_pton`, `getaddrinfo`)

Older compilers, particularly older MinGW distributions, may not fully support these features. The compatibility check performed by `check_mingw.bat` identifies these issues.

## Compatibility Layer

To address these issues, we've implemented a compatibility layer in `include/compat.h` that provides:

1. **Thread Support**: Fallback implementations of threading primitives using Windows API:
   - `std::mutex` using Windows `CRITICAL_SECTION`
   - `std::lock_guard` and `std::unique_lock` for RAII-style mutex locking
   - `std::condition_variable` using Windows `CONDITION_VARIABLE`
   - `std::thread` using Windows `CreateThread`

2. **Atomic Support**: A simplified `std::atomic` implementation using mutex-protected operations

3. **Network Function Replacements**:
   - Fallback implementation of `inet_pton` using `inet_addr`
   - Alternative hostname resolution using `gethostbyname` when `getaddrinfo` isn't available

4. **Lambda Replacements**: Regular functions with global state instead of lambdas

## Testing Compatibility

To test if your system is compatible:

1. Run `check_mingw.bat` first to see if your compiler supports C++11 features
2. Run `build_minimal.bat` to build minimal test examples
3. Run `bin\minimal_server.exe` in one terminal
4. Run `bin\minimal_worker.exe` in another terminal

If both run without errors, your system should be compatible with the full distributed web scraper.

## Recommended Compiler Setup

For best results, we recommend:

1. **MinGW-W64**: A more modern implementation of MinGW with better C++11 support
   - Download from: https://sourceforge.net/projects/mingw-w64/

2. **G++ 4.8.1 or newer**: Required for full C++11 threading support

3. **Windows 7 or newer**: Required for some modern Windows threading APIs

## Troubleshooting

If you encounter compilation errors:

1. Look for errors related to threading or networking functions
2. Ensure `include/compat.h` is included in your source files
3. Try building the minimal examples first to isolate issues
4. If necessary, modify the compatibility layer to support your specific compiler version

## Performance Considerations

The compatibility layer implementations may have reduced performance compared to native C++11 implementations:

1. The fallback mutex implementation may be slower than standard library mutexes
2. The atomic operations use mutexes, which introduce more overhead
3. Thread creation and management is more expensive with the Windows API directly

For production use, consider upgrading your compiler to a more modern version that fully supports C++11. 