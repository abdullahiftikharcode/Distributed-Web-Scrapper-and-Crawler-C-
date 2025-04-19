#include "../include/config.h"
#include "../include/compat.h"
#include <iostream>
#include <string>
#include <cstring>
#include <thread>

// Thread function
void worker_thread() {
    std::cout << "Thread is running!" << std::endl;
    
    // Sleep for a bit
    #ifdef _GLIBCXX_HAS_GTHREADS
    std::this_thread::sleep_for(std::chrono::seconds(2));
    #else
    // For our compatibility layer
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    #endif
    
    std::cout << "Thread is done!" << std::endl;
}

int main() {
    std::cout << "Starting minimal worker test..." << std::endl;
    
    // Test atomic
    std::atomic<int> counter(0);
    counter++;
    std::cout << "Atomic counter value: " << counter.load() << std::endl;
    
    // Test thread
    std::thread t(worker_thread);
    std::cout << "Thread created, waiting for it to finish..." << std::endl;
    
    // Join thread
    t.join();
    std::cout << "Thread joined successfully!" << std::endl;
    
    // Test socket
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
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
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
    serverAddr.sin_port = htons(9000);
    
    // Convert IP address from string to binary form
    #ifdef _WIN32
    // For Windows, use inet_addr
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    #else
    if (inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid IP address" << std::endl;
        CLOSE_SOCKET(clientSocket);
        SOCKET_CLEANUP;
        return 1;
    }
    #endif
    
    // Try to connect
    std::cout << "Attempting to connect to server..." << std::endl;
    if (connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        #ifdef _WIN32
        std::cerr << "Connect failed: " << WSAGetLastError() << std::endl;
        #else
        std::cerr << "Connect failed" << std::endl;
        #endif
        CLOSE_SOCKET(clientSocket);
        SOCKET_CLEANUP;
        return 1;
    }
    
    std::cout << "Connected to server successfully!" << std::endl;
    
    // Send data
    const char* message = "REGISTER";
    if (send(clientSocket, message, strlen(message), 0) == SOCKET_ERROR) {
        #ifdef _WIN32
        std::cerr << "Send failed: " << WSAGetLastError() << std::endl;
        #else
        std::cerr << "Send failed" << std::endl;
        #endif
        CLOSE_SOCKET(clientSocket);
        SOCKET_CLEANUP;
        return 1;
    }
    
    std::cout << "Message sent successfully!" << std::endl;
    std::cout << "Press Enter to exit." << std::endl;
    
    // Wait for user input
    std::cin.get();
    
    // Clean up
    CLOSE_SOCKET(clientSocket);
    SOCKET_CLEANUP;
    
    std::cout << "Worker shut down successfully." << std::endl;
    return 0;
} 