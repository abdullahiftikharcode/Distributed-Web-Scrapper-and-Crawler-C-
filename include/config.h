#ifndef CONFIG_H
#define CONFIG_H

#include <string.h>  // For memset

// Platform-specific configuration
#ifdef _WIN32
    // Windows-specific includes and definitions
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif

    #ifdef _MSC_VER
        // For Visual Studio
        #include <WinSock2.h>  // Must come before windows.h
        #include <WS2tcpip.h>
        #include <Windows.h>
        #pragma comment(lib, "Ws2_32.lib")
    #else
        // For MinGW
        #include <winsock2.h>  // Must come before windows.h
        #include <ws2tcpip.h>
        #include <windows.h>
    #endif
    
    // Link with Winsock libraries
    #ifdef _MSC_VER
        // Visual Studio specific
        // Already done with pragma comment above
    #endif
    
    // Windows socket cleanup helper
    #define SOCKET_CLEANUP WSACleanup()
    #define CLOSE_SOCKET(s) closesocket(s)
#else
    // Unix/Linux/Mac includes and definitions
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netdb.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    
    // Type definitions to match Windows
    typedef int SOCKET;
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    
    // Unix socket cleanup helper
    #define SOCKET_CLEANUP do {} while(0)
    #define CLOSE_SOCKET(s) close(s)
#endif

// Add ZeroMemory polyfill for non-Windows platforms
#ifndef _WIN32
    #define ZeroMemory(Destination, Length) memset((Destination), 0, (Length))
#endif

#endif // CONFIG_H 