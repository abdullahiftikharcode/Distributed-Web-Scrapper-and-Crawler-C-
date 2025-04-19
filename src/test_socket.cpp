#ifdef _MSC_VER
// Additional includes and defines for Visual Studio
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "../include/config.h"
#include <iostream>
#include <string>

int main() {
    std::cout << "Socket Test Program" << std::endl;
    
    #ifdef _WIN32
    // Initialize Winsock
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        return 1;
    }
    std::cout << "Winsock initialized successfully" << std::endl;
    
    // Check Winsock version
    std::cout << "Winsock version: " 
              << LOBYTE(wsaData.wVersion) << "." 
              << HIBYTE(wsaData.wVersion) << std::endl;
    std::cout << "Winsock description: " << wsaData.szDescription << std::endl;
    #else
    std::cout << "Running on non-Windows platform" << std::endl;
    #endif
    
    // Create a socket
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        #ifdef _WIN32
        std::cerr << "Error creating socket: " << WSAGetLastError() << std::endl;
        WSACleanup();
        #else
        std::cerr << "Error creating socket" << std::endl;
        #endif
        return 1;
    }
    std::cout << "Socket created successfully" << std::endl;
    
    // Test getaddrinfo
    struct addrinfo hints;
    struct addrinfo* addr_info = nullptr;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    
    result = getaddrinfo("example.com", "80", &hints, &addr_info);
    if (result != 0) {
        std::cerr << "getaddrinfo failed: " << result << std::endl;
    } else {
        std::cout << "getaddrinfo successfully resolved example.com" << std::endl;
        freeaddrinfo(addr_info);
    }
    
    // Close the socket
    CLOSE_SOCKET(sock);
    std::cout << "Socket closed" << std::endl;
    
    #ifdef _WIN32
    // Cleanup Winsock
    WSACleanup();
    std::cout << "Winsock cleaned up" << std::endl;
    #endif
    
    return 0;
} 