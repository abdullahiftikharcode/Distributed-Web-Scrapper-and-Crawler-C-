#ifdef _MSC_VER
// Additional includes and defines for Visual Studio
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "../include/HttpClient.h"
#include "../include/config.h"
#include "../include/compat.h"
#include <iostream>
#include <sstream>

std::string http_get(const std::string& hostname, const std::string& resource_path) {
    #ifdef _WIN32
    // Initialize Winsock
    WSADATA wsaData;
    int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsa_result != 0) {
        std::cerr << "WSAStartup failed: " << wsa_result << std::endl;
        return "";
    }
    #endif

    SOCKET sock = INVALID_SOCKET;
    std::string response;

    // First try using getaddrinfo if available
    #ifdef _WIN32
    // Check if getaddrinfo is available (it is in most modern Windows systems)
    HMODULE ws2_32 = GetModuleHandle("ws2_32.dll");
    if (ws2_32 != NULL) {
        typedef int (WSAAPI *PGETADDRINFO)(const char*, const char*, const struct addrinfo*, struct addrinfo**);
        typedef void (WSAAPI *PFREEADDRINFO)(struct addrinfo*);
        
        PGETADDRINFO pGetAddrInfo = (PGETADDRINFO)GetProcAddress(ws2_32, "getaddrinfo");
        PFREEADDRINFO pFreeAddrInfo = (PFREEADDRINFO)GetProcAddress(ws2_32, "freeaddrinfo");
        
        if (pGetAddrInfo != NULL && pFreeAddrInfo != NULL) {
            // Modern system, use getaddrinfo
            struct addrinfo hints;
            struct addrinfo* addr_info = nullptr;
            
            ZeroMemory(&hints, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;
            
            int result = (*pGetAddrInfo)(hostname.c_str(), "80", &hints, &addr_info);
            if (result == 0 && addr_info != nullptr) {
                // Create socket
                sock = socket(addr_info->ai_family, addr_info->ai_socktype, addr_info->ai_protocol);
                if (sock != INVALID_SOCKET) {
                    // Connect to the server
                    result = connect(sock, addr_info->ai_addr, (int)addr_info->ai_addrlen);
                    if (result == SOCKET_ERROR) {
                        std::cerr << "Error connecting to server via getaddrinfo: " << WSAGetLastError() << std::endl;
                        CLOSE_SOCKET(sock);
                        sock = INVALID_SOCKET;
                    }
                }
                (*pFreeAddrInfo)(addr_info);
            }
        }
    }
    #else
    // On non-Windows systems, use getaddrinfo directly
    struct addrinfo hints;
    struct addrinfo* addr_info = nullptr;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    
    int result = getaddrinfo(hostname.c_str(), "80", &hints, &addr_info);
    if (result == 0 && addr_info != nullptr) {
        // Create socket
        sock = socket(addr_info->ai_family, addr_info->ai_socktype, addr_info->ai_protocol);
        if (sock != INVALID_SOCKET) {
            // Connect to the server
            result = connect(sock, addr_info->ai_addr, (int)addr_info->ai_addrlen);
            if (result == SOCKET_ERROR) {
                std::cerr << "Error connecting to server via getaddrinfo" << std::endl;
                CLOSE_SOCKET(sock);
                sock = INVALID_SOCKET;
            }
        }
        freeaddrinfo(addr_info);
    }
    #endif

    // Fallback to older method if getaddrinfo failed or is not available
    if (sock == INVALID_SOCKET) {
        // Set up server address using older methods
        struct sockaddr_in serverAddr;
        ZeroMemory(&serverAddr, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(80);  // HTTP port
        
        // Convert hostname to IP address
        serverAddr.sin_addr.s_addr = inet_addr(hostname.c_str());
        
        // If hostname is not a valid IP address, try to resolve it
        if (serverAddr.sin_addr.s_addr == INADDR_NONE) {
            // Use gethostbyname (older but widely available)
            struct hostent* host = gethostbyname(hostname.c_str());
            if (host == nullptr) {
                std::cerr << "Failed to resolve hostname: " << hostname << std::endl;
                SOCKET_CLEANUP;
                return "";
            }
            // Copy the resolved IP address
            memcpy(&serverAddr.sin_addr, host->h_addr_list[0], host->h_length);
        }
        
        // Create socket
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            std::cerr << "Error creating socket in fallback method" << std::endl;
            SOCKET_CLEANUP;
            return "";
        }
        
        // Connect to the server
        if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "Error connecting to server in fallback method" << std::endl;
            CLOSE_SOCKET(sock);
            SOCKET_CLEANUP;
            return "";
        }
    }

    // If we get here, we have a valid connected socket
    // Add socket timeouts to prevent hanging forever on slow servers
    #ifdef _WIN32
    DWORD timeout = 30000;  // 30 seconds in milliseconds
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    #else
    struct timeval timeout;
    timeout.tv_sec = 30;  // 30 seconds timeout
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
    #endif
    
    // Build the HTTP request
    std::ostringstream request_stream;
    request_stream << "GET " << resource_path << " HTTP/1.1\r\n";
    request_stream << "Host: " << hostname << "\r\n";
    request_stream << "Connection: close\r\n";
    request_stream << "User-Agent: CustomScraper/1.0\r\n";
    request_stream << "\r\n";
    
    std::string request = request_stream.str();

    // Send the request
    int result = send(sock, request.c_str(), (int)request.length(), 0);
    if (result == SOCKET_ERROR) {
        #ifdef _WIN32
        std::cerr << "Error sending request: " << WSAGetLastError() << std::endl;
        #else
        std::cerr << "Error sending request" << std::endl;
        #endif
        CLOSE_SOCKET(sock);
        SOCKET_CLEANUP;
        return "";
    }

    // Receive the response
    const int buffer_size = 4096;
    char buffer[buffer_size];

    // Loop until connection is closed or error occurs
    do {
        result = recv(sock, buffer, buffer_size - 1, 0);
        if (result > 0) {
            buffer[result] = '\0';  // Null-terminate the received data
            response += buffer;
        } else if (result == 0) {
            // Connection closed
        } else {
            #ifdef _WIN32
            std::cerr << "Error receiving data: " << WSAGetLastError() << std::endl;
            #else
            std::cerr << "Error receiving data" << std::endl;
            #endif
            CLOSE_SOCKET(sock);
            SOCKET_CLEANUP;
            return "";
        }
    } while (result > 0);

    // Clean up
    CLOSE_SOCKET(sock);
    SOCKET_CLEANUP;

    return response;
}

std::string extract_body(const std::string& response) {
    // Find the header/body separator (the first occurrence of "\r\n\r\n")
    size_t pos = response.find("\r\n\r\n");
    if (pos == std::string::npos) {
        return "";
    }
    
    // Return the body (everything after the separator)
    return response.substr(pos + 4);
} 