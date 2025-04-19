#include "../include/config.h"
#include "../include/compat.h"
#include <iostream>
#include <string>
#include <cstring>

int main() {
    std::cout << "Starting minimal server test..." << std::endl;
    
    #ifdef _WIN32
    // Initialize Winsock on Windows
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        return 1;
    }
    #endif
    
    // Create socket
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        #ifdef _WIN32
        std::cerr << "Error creating socket: " << WSAGetLastError() << std::endl;
        WSACleanup();
        #else
        std::cerr << "Error creating socket" << std::endl;
        #endif
        return 1;
    }
    
    std::cout << "Socket created successfully!" << std::endl;
    
    // Set up server address
    struct sockaddr_in serverAddr;
    ZeroMemory(&serverAddr, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(9000);
    
    // Try to bind
    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        #ifdef _WIN32
        std::cerr << "Bind failed: " << WSAGetLastError() << std::endl;
        #else
        std::cerr << "Bind failed" << std::endl;
        #endif
        CLOSE_SOCKET(serverSocket);
        SOCKET_CLEANUP;
        return 1;
    }
    
    std::cout << "Bind successful!" << std::endl;
    
    // Try to listen
    if (listen(serverSocket, 5) == SOCKET_ERROR) {
        #ifdef _WIN32
        std::cerr << "Listen failed: " << WSAGetLastError() << std::endl;
        #else
        std::cerr << "Listen failed" << std::endl;
        #endif
        CLOSE_SOCKET(serverSocket);
        SOCKET_CLEANUP;
        return 1;
    }
    
    std::cout << "Server is listening on port 9000..." << std::endl;
    std::cout << "Press Enter to exit." << std::endl;
    
    // Wait for user input
    std::cin.get();
    
    // Clean up
    CLOSE_SOCKET(serverSocket);
    SOCKET_CLEANUP;
    
    std::cout << "Server shut down successfully." << std::endl;
    return 0;
} 