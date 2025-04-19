/**
 * Web Scraper - Distributed Version
 * Worker Implementation
 * 
 * This worker connects to the central server, registers,
 * performs web scraping tasks, and reports progress.
 */

#ifdef _MSC_VER
// Additional includes and defines for Visual Studio
#define NOMINMAX
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif

// Undefine any potentially conflicting macros
#ifdef milli
#undef milli
#endif

#include "../include/config.h"
#include "../include/compat.h"
#include "../include/Book.h"
#include "../include/Crawler.h"
#include "../include/HtmlParser.h"
#include "../include/HttpClient.h"
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <cstring>
#include <iomanip> // For put_time
#include <queue>
#include <set>

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#endif

// Buffer size for receiving data
const int BUFFER_SIZE = 4096;

// Global counter for processed pages
std::atomic<int> processedPages(0);

// Global flag to indicate if processing should stop
std::atomic<bool> shouldStop(false);

// Global mutex and condition variable for thread synchronization
std::mutex mtx;
std::condition_variable cv;

// Worker ID assigned by the server
int workerId = -1;

// Add this near the top of the file with other global variables
std::string lastReceivedUrl;
std::set<std::string> processedUrls;
std::mutex processedUrlsMutex;
std::string startUrl; // Store the starting URL for reference

// Add this near other global variables
std::vector<Book> recentlyProcessedBooks;
std::mutex recentBooksLock;
const size_t MAX_RECENT_BOOKS = 50; // Keep track of the last 50 books

// Add a new mutex near other global variables
std::mutex socketMutex;

// Add a new atomic variable near other globals
std::atomic<bool> mainThreadCommunicating(false);

// Forward function declarations
std::string getBaseUrl(const std::string& hostname);
std::string getUrlFromServer(SOCKET serverSocket);
bool sendProcessedUrlToServer(SOCKET serverSocket, const std::string& url, const Book& book, const std::vector<std::string>& links);
SOCKET connectToServer(const std::string& serverIP, int serverPort);
void progressReporter(SOCKET serverSocket);
bool sendProgressUpdate(SOCKET serverSocket, int count);
bool should_stop_predicate();
Book crawl_page(const std::string& hostname, const std::string& page_url);
std::pair<Book, std::string> crawl_page_with_html(const std::string& hostname, const std::string& page_url);

// Function to get current timestamp as string
std::string getTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_time), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// Log with timestamp
void log(const std::string& message) {
    std::cout << "[" << getTimestamp() << "] " << message << std::endl;
}

// Connect to the server and register
SOCKET connectToServer(const std::string& serverIP, int serverPort) {
    #ifdef _WIN32
    // Initialize Winsock on Windows
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        return INVALID_SOCKET;
    }
    #endif
    
    // Create socket
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        #ifdef _WIN32
        std::cerr << "Error creating socket: " << WSAGetLastError() << std::endl;
        WSACleanup();
        #else
        std::cerr << "Error creating socket" << std::endl;
        #endif
        return INVALID_SOCKET;
    }
    
    // Set up server address
    struct sockaddr_in serverAddr;
    ZeroMemory(&serverAddr, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<u_short>(serverPort));
    
    // Convert IP address from string to binary form
    if (inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid server IP address" << std::endl;
        CLOSE_SOCKET(sock);
        SOCKET_CLEANUP;
        return INVALID_SOCKET;
    }
    
    // Connect to the server
    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        #ifdef _WIN32
        std::cerr << "Error connecting to server: " << WSAGetLastError() << std::endl;
        #else
        std::cerr << "Error connecting to server" << std::endl;
        #endif
        CLOSE_SOCKET(sock);
        SOCKET_CLEANUP;
        return INVALID_SOCKET;
    }
    
    log("Connected to server at " + serverIP + ":" + std::to_string(serverPort));
    
    // Send registration message
    std::string registerMsg = "REGISTER";
    if (send(sock, registerMsg.c_str(), registerMsg.length(), 0) == SOCKET_ERROR) {
        #ifdef _WIN32
        std::cerr << "Error sending registration message: " << WSAGetLastError() << std::endl;
        #else
        std::cerr << "Error sending registration message" << std::endl;
        #endif
        CLOSE_SOCKET(sock);
        SOCKET_CLEANUP;
        return INVALID_SOCKET;
    }
    
    log("Sent registration message to server");
    
    // Receive worker ID from server
    char buffer[BUFFER_SIZE];
    int bytesReceived = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    if (bytesReceived <= 0) {
        std::cerr << "Error receiving worker ID from server" << std::endl;
        CLOSE_SOCKET(sock);
        SOCKET_CLEANUP;
        return INVALID_SOCKET;
    }
    
    // Null-terminate the received data
    buffer[bytesReceived] = '\0';
    
    // Parse the worker ID
    std::string response(buffer);
    if (response.find("ASSIGN_ID:") != 0) {
        std::cerr << "Invalid response from server: " << response << std::endl;
        CLOSE_SOCKET(sock);
        SOCKET_CLEANUP;
        return INVALID_SOCKET;
    }
    
    std::string idStr = response.substr(10); // Skip "ASSIGN_ID:"
    workerId = std::stoi(idStr);
    
    log("Registered with server. Assigned worker ID: " + std::to_string(workerId));
    
    return sock;
}

// Send progress update to the server
bool sendProgressUpdate(SOCKET serverSocket, int count) {
    // Protect socket access with mutex
    std::lock_guard<std::mutex> lock(socketMutex);
    
    // Create progress message
    std::string progressMsg = "PROGRESS:" + std::to_string(count);
    
    // Record start time for latency measurement
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Send progress update
    if (send(serverSocket, progressMsg.c_str(), progressMsg.length(), 0) == SOCKET_ERROR) {
        #ifdef _WIN32
        std::cerr << "Error sending progress update: " << WSAGetLastError() << std::endl;
        #else
        std::cerr << "Error sending progress update" << std::endl;
        #endif
        return false;
    }
    
    // Receive acknowledgment
    char buffer[BUFFER_SIZE];
    int bytesReceived = recv(serverSocket, buffer, BUFFER_SIZE - 1, 0);
    if (bytesReceived <= 0) {
        std::cerr << "Error receiving acknowledgment from server" << std::endl;
        return false;
    }
    
    // Record end time and calculate latency
    auto endTime = std::chrono::high_resolution_clock::now();
    auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    
    // Null-terminate the received data
    buffer[bytesReceived] = '\0';
    
    // Check the response
    std::string response(buffer);
    
    // If server sent shutdown signal, indicate to stop
    if (response == "SHUTDOWN") {
        log("Received shutdown signal from server!");
        shouldStop.store(true);
        return false;
    }
    
    // If server sent a URL, it's not an error - just ignore it for now
    // The next getUrlFromServer() call will handle it
    if (response.find("URL:") == 0) {
        // This is a URL response, which is valid but unexpected here
        // Just acknowledge it silently and continue
        return true;
    }
    
    // If server is telling us to wait, that's okay - the crawler is disabled
    if (response == "WAIT") {
        log("Server response: WAIT. Crawler may be paused. Continuing to wait for activation.");
        return true;
    }
    
    // Otherwise, it should be an acknowledgment
    if (response != "ACK") {
        std::cerr << "Invalid acknowledgment from server: " << response << std::endl;
        // Don't return false here, just log it and continue
        log("Unexpected response, but continuing to run. Will retry later.");
        return true;
    }
    
    log("Progress update sent: " + std::to_string(count) + 
        " pages processed (network latency: " + std::to_string(latency) + "ms)");
    
    return true;
}

// Predicate function for condition variable (to replace lambda)
bool should_stop_predicate() {
    return shouldStop.load();
}

// Helper function to get the base URL from a hostname
std::string getBaseUrl(const std::string& hostname) {
    // Make sure the hostname doesn't already have a protocol
    if (hostname.find("http://") == 0 || hostname.find("https://") == 0) {
        return hostname;
    }
    return "http://" + hostname;
}

// Check if a URL is valid
bool isValidUrl(const std::string& url) {
    // Basic validation: URL should contain http:// or https:// and not have double protocols
    if (url.empty()) {
        return false;
    }
    
    // Check for malformed URLs with double protocols
    if (url.find("http://http://") != std::string::npos || 
        url.find("http://https://") != std::string::npos ||
        url.find("https://http://") != std::string::npos ||
        url.find("https://https://") != std::string::npos) {
        return false;
    }
    
    // Check for malformed URLs with domain concatenation
    if (url.find("http://books.toscrape.comhttp") != std::string::npos ||
        url.find("http://books.toscrape.comhttps") != std::string::npos) {
        return false;
    }
    
    return true;
}

// Fix malformed URLs
std::string fixMalformedUrl(const std::string& url) {
    // If URL contains domain concatenation, fix it
    if (url.find("http://books.toscrape.comhttp") != std::string::npos) {
        size_t pos = url.find("http://books.toscrape.comhttp");
        return url.substr(pos + 24); // Skip the duplicate part
    }
    
    if (url.find("http://books.toscrape.comhttps") != std::string::npos) {
        size_t pos = url.find("http://books.toscrape.comhttps");
        return url.substr(pos + 24); // Skip the duplicate part
    }
    
    return url;
}

// Get a URL from the server to process
std::string getUrlFromServer(SOCKET serverSocket) {
    // Set the flag to indicate main thread is communicating
    mainThreadCommunicating.store(true);
    // Use RAII to ensure we reset the flag on function exit
    struct FlagResetter {
        ~FlagResetter() { mainThreadCommunicating.store(false); }
    } flagResetter;
    
    // Protect socket access with mutex
    std::lock_guard<std::mutex> lock(socketMutex);
    
    // Keep track of wait count for progressive backoff
    static int waitCount = 0;
    static int errorCount = 0; // Track consecutive errors
    const int MAX_CONSECUTIVE_ERRORS = 5; // Maximum allowed consecutive errors
    
    try {
        // If we have a stored URL from a previous out-of-sync response, use it
        if (!lastReceivedUrl.empty()) {
            std::string url = lastReceivedUrl;
            lastReceivedUrl.clear();
            
            // Fix any malformed URLs before processing
            url = fixMalformedUrl(url);
            
            // Reset wait count and error count when using a cached URL
            waitCount = 0;
            errorCount = 0;
            
            if (isValidUrl(url)) {
                return url;
            } else {
                log("Skipping invalid cached URL: " + url);
                return "";
            }
        }
        
        // Send URL request
        std::string requestMsg = "GET_URL";
        if (send(serverSocket, requestMsg.c_str(), requestMsg.length(), 0) == SOCKET_ERROR) {
            errorCount++;
            log("Error sending URL request to server (attempt " + std::to_string(errorCount) + ")");
            if (errorCount >= MAX_CONSECUTIVE_ERRORS) {
                log("Too many consecutive errors, triggering stop");
                shouldStop.store(true);
            }
            return "";
        }
        
        // Receive response with timeout
        char buffer[BUFFER_SIZE];
        int bytesReceived = recv(serverSocket, buffer, BUFFER_SIZE - 1, 0);
        if (bytesReceived <= 0) {
            errorCount++;
            log("Error receiving URL from server (attempt " + std::to_string(errorCount) + ")");
            if (errorCount >= MAX_CONSECUTIVE_ERRORS) {
                log("Too many consecutive errors, triggering stop");
                shouldStop.store(true);
            }
            return "";
        }
        
        // Reset error count on successful communication
        errorCount = 0;
        
        // Null-terminate the received data
        buffer[bytesReceived] = '\0';
        
        // Parse the response
        std::string response(buffer);
        
        if (response == "SHUTDOWN") {
            log("Received shutdown signal from server");
            shouldStop.store(true);
            return "";
        }
        
        if (response == "WAIT") {
            // No URLs available at the moment, wait and try again
            log("No URLs available at the moment, waiting before retry");
            
            // Use a progressive backoff strategy
            static const int MAX_WAIT_COUNT = 10; // Cap the wait count to avoid overflow
            
            // Exponential backoff with a maximum of 10 seconds
            int retryTime = std::min(3000 * (waitCount + 1), 10000); // 3-10 seconds
            
            // Increment wait count for next time, capping at MAX_WAIT_COUNT
            waitCount = std::min(waitCount + 1, MAX_WAIT_COUNT);
            
            log("Waiting for " + std::to_string(retryTime / 1000) + " seconds before retry");
            std::this_thread::sleep_for(std::chrono::milliseconds(retryTime));
            return "";
        }
        
        if (response == "ACK") {
            // Server sent an ACK, which is normal after processing a URL
            // Don't wait, immediately request a new URL
            log("Received ACK from server, requesting next URL");
            
            // Send URL request immediately
            std::string requestMsg = "GET_URL";
            if (send(serverSocket, requestMsg.c_str(), requestMsg.length(), 0) == SOCKET_ERROR) {
                log("Error sending URL request to server after ACK");
                return "";
            }
            
            // Receive response for the new URL request
            bytesReceived = recv(serverSocket, buffer, BUFFER_SIZE - 1, 0);
            if (bytesReceived <= 0) {
                log("Error receiving URL from server after ACK");
                return "";
            }
            
            // Null-terminate the received data
            buffer[bytesReceived] = '\0';
            
            // Parse the new response
            response = std::string(buffer);
            
            // Continue with normal response processing below
            // (The rest of the function will handle this new response)
        }
        
        if (response.find("URL:") == 0) {
            // Extract the URL
            std::string url = response.substr(4); // Skip "URL:"
            
            // Reset wait count when we get a URL
            waitCount = 0;
            
            // Fix any malformed URLs before processing
            url = fixMalformedUrl(url);
            
            if (isValidUrl(url)) {
                return url;
            } else {
                log("Skipping invalid URL received from server: " + url);
                // Send ACK to acknowledge receipt of invalid URL
                std::string ackMsg = "ACK";
                send(serverSocket, ackMsg.c_str(), ackMsg.length(), 0);
                return "";
            }
        }
        
        log("Received invalid response from server: " + response);
        return "";
    }
    catch (const std::exception& e) {
        log("Exception in getUrlFromServer: " + std::string(e.what()));
        errorCount++;
        if (errorCount >= MAX_CONSECUTIVE_ERRORS) {
            log("Too many consecutive errors in getUrlFromServer, triggering stop");
            shouldStop.store(true);
        }
        return "";
    }
}

// Send processed URL and book data back to server
bool sendProcessedUrlToServer(SOCKET serverSocket, const std::string& url, const Book& book, const std::vector<std::string>& links) {
    // Set the flag to indicate main thread is communicating
    mainThreadCommunicating.store(true);
    // Use RAII to ensure we reset the flag on function exit
    struct FlagResetter {
        ~FlagResetter() { mainThreadCommunicating.store(false); }
    } flagResetter;
    
    // Protect socket access with mutex
    std::lock_guard<std::mutex> lock(socketMutex);
    
    // Limit the number of links to send to avoid buffer overflow
    const size_t MAX_LINKS_PER_MESSAGE = 20;
    
    try {
        // Format the book data
        std::string bookData = "{url:\"" + url + "\",book:{title:\"" + book.title + 
                              "\",price:\"" + book.price + "\",rating:\"" + book.rating + 
                              "\",url:\"" + book.url + "\"}}";
        
        // First send the PROCESSED message with book data but no links
        std::string initialMsg = "PROCESSED:" + bookData;
        
        // Send the initial message
        if (send(serverSocket, initialMsg.c_str(), initialMsg.length(), 0) == SOCKET_ERROR) {
            log("Error sending processed URL data to server");
            return false;
        }
        
        // Set up timeout for waiting for server response
        const auto startTime = std::chrono::steady_clock::now();
        const int MAX_WAIT_TIME_SEC = 30; // 30 second timeout
        
        // Wait for acknowledgment with timeout
        char buffer[BUFFER_SIZE];
        int bytesReceived = 0;
        
        // Use non-blocking socket to implement timeout
        #ifdef _WIN32
        u_long mode = 1; // 1 to enable non-blocking socket
        ioctlsocket(serverSocket, FIONBIO, &mode);
        #else
        int flags = fcntl(serverSocket, F_GETFL, 0);
        fcntl(serverSocket, F_SETFL, flags | O_NONBLOCK);
        #endif
        
        // Keep trying to receive until we get a response or timeout
        while (bytesReceived <= 0) {
            bytesReceived = recv(serverSocket, buffer, BUFFER_SIZE - 1, 0);
            
            if (bytesReceived > 0) {
                // We got data, break out
                break;
            } else {
                // Check if we've timed out
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count() >= MAX_WAIT_TIME_SEC) {
                    log("Timeout waiting for server acknowledgment after " + std::to_string(MAX_WAIT_TIME_SEC) + " seconds");
                    
                    // Return socket to blocking mode
                    #ifdef _WIN32
                    mode = 0; // 0 to disable non-blocking socket
                    ioctlsocket(serverSocket, FIONBIO, &mode);
                    #else
                    fcntl(serverSocket, F_SETFL, flags & ~O_NONBLOCK);
                    #endif
                    
                    return false;
                }
                
                // Small sleep to prevent tight loop
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        
        // Return socket to blocking mode
        #ifdef _WIN32
        mode = 0; // 0 to disable non-blocking socket
        ioctlsocket(serverSocket, FIONBIO, &mode);
        #else
        fcntl(serverSocket, F_SETFL, flags & ~O_NONBLOCK);
        #endif
        
        // Null-terminate the received data
        buffer[bytesReceived] = '\0';
        std::string response(buffer);
        
        // Handle special case for doubled messages like "ACKACK"
        if (response.find("ACK") == 0 && response.length() > 3) {
            log("Received malformed response: " + response + ", treating as ACK");
            response = "ACK";
        }
        
        // Handle response to initial message
        if (response != "ACK") {
            if (response.find("URL:") == 0) {
                // Store URL for next request
                std::string nextUrl = response.substr(4);
                nextUrl = fixMalformedUrl(nextUrl);
                if (isValidUrl(nextUrl)) {
                    lastReceivedUrl = nextUrl;
                    log("Stored URL from server for next request: " + nextUrl);
                }
            } else if (response == "SHUTDOWN") {
                log("Received shutdown signal from server");
                shouldStop.store(true);
                return false;
            } else {
                log("Unexpected response to initial processed message: " + response);
                return false;
            }
        }
        
        // Now send links in batches if there are any, with timeout for each batch
        if (!links.empty()) {
            // Calculate number of batches needed
            size_t numBatches = (links.size() + MAX_LINKS_PER_MESSAGE - 1) / MAX_LINKS_PER_MESSAGE;
            
            for (size_t batch = 0; batch < numBatches; batch++) {
                // Calculate start and end indices for this batch
                size_t startIdx = batch * MAX_LINKS_PER_MESSAGE;
                size_t endIdx = std::min(startIdx + MAX_LINKS_PER_MESSAGE, links.size());
                
                // Format the links for this batch
                std::string linksStr = "";
                for (size_t i = startIdx; i < endIdx; ++i) {
                    if (i > startIdx) linksStr += ",";
                    linksStr += "\"" + links[i] + "\"";
                }
                
                // Format the links message
                std::string linksMsg = "LINKS:" + std::to_string(batch + 1) + "/" + 
                                       std::to_string(numBatches) + ":{url:\"" + 
                                       url + "\",links:[" + linksStr + "]}";
                
                // Send the links batch
                if (send(serverSocket, linksMsg.c_str(), linksMsg.length(), 0) == SOCKET_ERROR) {
                    log("Error sending links batch " + std::to_string(batch + 1) + " to server");
                    return false;
                }
                
                // Wait for acknowledgment with timeout for this batch
                const auto batchStartTime = std::chrono::steady_clock::now();
                bytesReceived = 0;
                
                // Use non-blocking socket to implement timeout
                #ifdef _WIN32
                u_long batchMode = 1;
                ioctlsocket(serverSocket, FIONBIO, &batchMode);
                #else
                int batchFlags = fcntl(serverSocket, F_GETFL, 0);
                fcntl(serverSocket, F_SETFL, batchFlags | O_NONBLOCK);
                #endif
                
                // Keep trying to receive until we get a response or timeout
                while (bytesReceived <= 0) {
                    bytesReceived = recv(serverSocket, buffer, BUFFER_SIZE - 1, 0);
                    
                    if (bytesReceived > 0) {
                        // We got data, break out
                        break;
                    } else {
                        // Check if we've timed out
                        auto now = std::chrono::steady_clock::now();
                        if (std::chrono::duration_cast<std::chrono::seconds>(now - batchStartTime).count() >= MAX_WAIT_TIME_SEC) {
                            log("Timeout waiting for server acknowledgment for links batch after " + std::to_string(MAX_WAIT_TIME_SEC) + " seconds");
                            
                            // Return socket to blocking mode
                            #ifdef _WIN32
                            batchMode = 0;
                            ioctlsocket(serverSocket, FIONBIO, &batchMode);
                            #else
                            fcntl(serverSocket, F_SETFL, batchFlags);
                            #endif
                            
                            return false;
                        }
                        
                        // Small sleep to prevent tight loop
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                }
                
                // Return socket to blocking mode
                #ifdef _WIN32
                batchMode = 0;
                ioctlsocket(serverSocket, FIONBIO, &batchMode);
                #else
                fcntl(serverSocket, F_SETFL, batchFlags & ~O_NONBLOCK);
                #endif
                
                // Null-terminate the received data
                buffer[bytesReceived] = '\0';
                response = std::string(buffer);
                
                // Handle special case for doubled messages like "ACKACK"
                if (response.find("ACK") == 0 && response.length() > 3) {
                    log("Received malformed batch response: " + response + ", treating as ACK");
                    response = "ACK";
                }
                
                // Handle response to links batch
                if (response != "ACK") {
                    if (response == "SHUTDOWN") {
                        log("Received shutdown signal from server while sending links");
                        shouldStop.store(true);
                        return false;
                    } else if (response.find("URL:") == 0) {
                        // Store URL for next request
                        std::string nextUrl = response.substr(4);
                        nextUrl = fixMalformedUrl(nextUrl);
                        if (isValidUrl(nextUrl)) {
                            lastReceivedUrl = nextUrl;
                            log("Stored URL from server for next request after links: " + nextUrl);
                        }
                    } else {
                        log("Unexpected response to links batch: " + response);
                        // Continue anyway, we've already processed the URL
                    }
                }
            }
        }
        
        return true;
    }
    catch (const std::exception& e) {
        log("Exception in sendProcessedUrlToServer: " + std::string(e.what()));
        return false;
    }
}

// Thread function for sending progress updates
void progressReporter(SOCKET serverSocket) {
    int lastReportedCount = 0;
    
    while (!shouldStop.load()) {
        // Sleep for a while
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait_for(lock, std::chrono::seconds(2), should_stop_predicate);
        }
        
        // Check if we should stop
        if (shouldStop.load()) {
            break;
        }
        
        // Don't send updates if main thread is communicating with server
        if (mainThreadCommunicating.load()) {
            continue;
        }
        
        // Get current processed count
        int currentCount = processedPages.load();
        
        // Send progress update if count has changed
        if (currentCount != lastReportedCount) {
            if (!sendProgressUpdate(serverSocket, currentCount)) {
                std::cerr << "Failed to send progress update or received shutdown signal. Exiting." << std::endl;
                shouldStop.store(true);
                break;
            }
            lastReportedCount = currentCount;
        }
    }
    
    log("Progress reporter thread terminated due to shutdown signal");
}

// Forward declaration for parse_book_page from HtmlParser.h
Book parse_book_page(const std::string& html, const std::string& hostname, const std::string& url);

// Modified crawl_page function that updates the global counter and returns both the book and HTML
std::pair<Book, std::string> crawl_page_with_html(const std::string& hostname, const std::string& page_url) {
    // First, sanitize the URL to fix any issues
    std::string valid_url = page_url;
    
    // Fix malformed URLs with domain concatenation
    if (valid_url.find("http://books.toscrape.comhttp") != std::string::npos ||
        valid_url.find("http://books.toscrape.comhttps") != std::string::npos) {
        size_t pos = valid_url.find("http://books.toscrape.com");
        if (pos != std::string::npos) {
            valid_url = valid_url.substr(pos + 24); // Skip duplicate domain
            if (valid_url.empty() || valid_url[0] != 'h') {
                valid_url = "https://books.toscrape.com/";
            }
        }
    }
    
    // Fix URLs that start with 'm'
    if (valid_url.find("mhttp") == 0) {
        valid_url = valid_url.substr(1); // Remove the 'm'
    }
    
    // Validate URL before crawling
    if (!isValidUrl(valid_url)) {
        log("Skipping invalid URL: " + page_url);
        return {Book(), ""};
    }
    
    log("Crawling page: " + valid_url);
    
    // Record start time
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Extract hostname from URL if needed
    std::string effective_hostname = hostname;
    if (valid_url.find("https://") == 0 || valid_url.find("http://") == 0) {
        size_t domain_start = valid_url.find("://") + 3;
        size_t domain_end = valid_url.find('/', domain_start);
        if (domain_end != std::string::npos) {
            effective_hostname = valid_url.substr(domain_start, domain_end - domain_start);
        } else {
            effective_hostname = valid_url.substr(domain_start);
        }
    }
    
    // Make HTTP request to get the page content
    std::string response;
    try {
        response = http_get(effective_hostname, valid_url);
    } catch (const std::exception& e) {
        log("Error fetching URL: " + valid_url + " - " + e.what());
        return {Book(), ""};
    }
    
    if (response.empty()) {
        log("Empty response from URL: " + valid_url);
        return {Book(), ""};
    }
    
    std::string html = extract_body(response);
    
    // Parse the HTML to extract book information
    Book book = parse_book_page(html, effective_hostname, valid_url);
    
    // Check if this is a duplicate or very similar book to recently processed ones
    if (!book.title.empty()) {
        std::lock_guard<std::mutex> lock(recentBooksLock);
        
        // Check recently processed books for duplicates or similar titles
        bool isDuplicate = false;
        for (const auto& recentBook : recentlyProcessedBooks) {
            // Check for exact match (title, price, rating)
            if (recentBook.title == book.title && 
                recentBook.price == book.price && 
                recentBook.rating == book.rating) {
                log("Skipping duplicate book: " + book.title);
                isDuplicate = true;
                break;
            }
            
            // Check for similar titles (75% similarity)
            if (recentBook.title.length() > 0 && book.title.length() > 0) {
                // Simple similarity check - if the longer title contains the shorter one
                const std::string& shorter = recentBook.title.length() < book.title.length() ? 
                                            recentBook.title : book.title;
                const std::string& longer = recentBook.title.length() >= book.title.length() ? 
                                           recentBook.title : book.title;
                
                // Convert both to lowercase for comparison
                std::string shorterLower = shorter;
                std::string longerLower = longer;
                std::transform(shorterLower.begin(), shorterLower.end(), shorterLower.begin(),
                              [](unsigned char c) { return std::tolower(c); });
                std::transform(longerLower.begin(), longerLower.end(), longerLower.begin(),
                              [](unsigned char c) { return std::tolower(c); });
                
                if (longerLower.find(shorterLower) != std::string::npos) {
                    log("Skipping similar book: " + book.title + " (similar to: " + recentBook.title + ")");
                    isDuplicate = true;
                    break;
                }
            }
        }
        
        if (!isDuplicate) {
            // Add to recently processed books
            recentlyProcessedBooks.push_back(book);
            
            // Keep the list at a reasonable size
            if (recentlyProcessedBooks.size() > MAX_RECENT_BOOKS) {
                recentlyProcessedBooks.erase(recentlyProcessedBooks.begin());
            }
        } else {
            // If it's a duplicate, clear the book info
            book = Book();
        }
    }
    
    // Record end time and calculate processing time
    auto endTime = std::chrono::high_resolution_clock::now();
    auto processingTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    
    log("Processed page: " + valid_url + " in " + std::to_string(processingTime) + "ms");
    
    // Increment the processed pages counter
    processedPages++;
    
    return {book, html};
}

// Original crawl_page function for backward compatibility
Book crawl_page(const std::string& hostname, const std::string& page_url) {
    return crawl_page_with_html(hostname, page_url).first;
}

// Add this function to check if a URL has been processed
bool hasUrlBeenProcessed(const std::string& url) {
    std::string canonicalUrl = url;
    
    // Convert to lowercase
    std::transform(canonicalUrl.begin(), canonicalUrl.end(), canonicalUrl.begin(),
                  [](unsigned char c) { return std::tolower(c); });
    
    // Remove trailing slash if present
    if (!canonicalUrl.empty() && canonicalUrl.back() == '/') {
        canonicalUrl.pop_back();
    }
    
    // For book/product pages, try to normalize the URL to handle duplicates
    // Example: If URLs contain product identifiers in different paths but point to the same product
    if (canonicalUrl.find("/catalogue/") != std::string::npos && 
        canonicalUrl.find(".html") != std::string::npos) {
        
        // Extract the product name and any identifiers
        size_t lastSlash = canonicalUrl.find_last_of('/');
        size_t dotHtml = canonicalUrl.find(".html");
        
        if (lastSlash != std::string::npos && dotHtml != std::string::npos) {
            // Get the product identifier/slug from the URL
            std::string productIdentifier = canonicalUrl.substr(lastSlash + 1, dotHtml - lastSlash - 1);
            
            // Find the main part without numeric ID
            size_t underscorePos = productIdentifier.find('_');
            if (underscorePos != std::string::npos) {
                // Focus on the product name part before the underscore/ID
                std::string productName = productIdentifier.substr(0, underscorePos);
                
                // Check if any URL with this product name has been processed
                std::lock_guard<std::mutex> lock(processedUrlsMutex);
                for (const auto& processedUrl : processedUrls) {
                    if (processedUrl.find(productName) != std::string::npos) {
                        log("Skipping duplicate product URL with different path: " + url);
                        return true;
                    }
                }
            }
        }
    }
    
    std::lock_guard<std::mutex> lock(processedUrlsMutex);
    bool exists = processedUrls.find(canonicalUrl) != processedUrls.end();
    if (!exists) {
        processedUrls.insert(canonicalUrl);
    }
    return exists;
}

// Extract and filter links from HTML
std::vector<std::string> find_all_links(const std::string& html, const std::string& hostname, const std::string& url) {
    // First extract all links using the existing function
    std::set<std::string> links = extract_all_links(html, url);
    
    // Filter out non-content URLs (static resources, etc.)
    std::set<std::string> filteredLinks;
    for (const auto& link : links) {
        // Skip static resources and non-content URLs
        if (link.find("/static/") != std::string::npos ||
            link.find(".css") != std::string::npos ||
            link.find(".js") != std::string::npos ||
            link.find(".ico") != std::string::npos ||
            link.find(".jpg") != std::string::npos ||
            link.find(".png") != std::string::npos) {
            continue;
        }
        
        // Skip malformed URLs
        if (link.find("http://books.toscrape.comhttp") != std::string::npos ||
            link.find("http://books.toscrape.comhttps") != std::string::npos ||
            link.find("mhttp") != std::string::npos ||
            link.find("mhttps") != std::string::npos) {
            continue;
        }
        
        filteredLinks.insert(link);
    }
    
    // Convert the filtered set to a vector
    std::vector<std::string> linksVector(filteredLinks.begin(), filteredLinks.end());
    
    // Log the number of links found
    log("Found " + std::to_string(filteredLinks.size()) + " filtered links out of " + 
        std::to_string(links.size()) + " total links on page " + url);
    
    return linksVector;
}

// Entry point
int main(int argc, char* argv[]) {
    // Default settings
    std::string serverIP = "127.0.0.1";
    int serverPort = 9000;
    std::string hostname = "books.toscrape.com";
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-s" || arg == "--server") {
            if (i + 1 < argc) {
                serverIP = argv[++i];
            }
        }
        else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                try {
                    serverPort = std::stoi(argv[++i]);
                } catch (const std::exception& e) {
                    std::cerr << "Invalid port number" << std::endl;
                    return 1;
                }
            }
        }
        else if (arg == "-h" || arg == "--hostname") {
            if (i + 1 < argc) {
                hostname = argv[++i];
            }
        }
        else if (arg == "--help") {
            std::cout << "Worker Usage:" << std::endl;
            std::cout << "  worker [options]" << std::endl;
            std::cout << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  -s, --server IP      Server IP address (default: 127.0.0.1)" << std::endl;
            std::cout << "  -p, --port PORT      Server port (default: 9000)" << std::endl;
            std::cout << "  -h, --hostname HOST  Website hostname (default: books.toscrape.com)" << std::endl;
            std::cout << "  --help               Show this help message" << std::endl;
            return 0;
        }
    }
    
    // Main worker loop - reconnect if disconnected
    while (true) {
        // Reset the should stop flag before each connection attempt
        shouldStop.store(false);
        
        log("Connecting to server at " + serverIP + ":" + std::to_string(serverPort));
        
        // Connect to the server
        SOCKET serverSocket = connectToServer(serverIP, serverPort);
        if (serverSocket == INVALID_SOCKET) {
            std::cerr << "Failed to connect to server, retrying in 5 seconds..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }
        
        log("Connected to server successfully");
        
        // Start progress reporter thread
        std::thread reporterThread(progressReporter, serverSocket);
        
        // Record start time
        auto startTime = std::chrono::high_resolution_clock::now();
        
        // Start crawling
        log("Starting worker for " + hostname + " - getting URLs from server");
        
        std::vector<Book> books;
        std::string baseUrl = getBaseUrl(hostname);
        // Store the base URL as our seed/start URL for reference
        startUrl = baseUrl;
        log("Set seed URL: " + startUrl);
        
        // Main loop - get URLs from server and process them
        while (!shouldStop.load()) {
            // Add a heartbeat log to track activity
            static auto lastHeartbeat = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - lastHeartbeat).count() >= 60) {
                log("Worker heartbeat - still running, processed " + std::to_string(processedPages.load()) + " pages");
                lastHeartbeat = now;
            }
            
            try {
                // Request a URL from the server
                std::string url = getUrlFromServer(serverSocket);
                
                // Check if we should stop or if there are no URLs available
                if (url.empty()) {
                    // Wait before trying again if we got an empty URL
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                    continue;
                }
                
                // Check if this URL has already been processed, or is a near-duplicate
                if (hasUrlBeenProcessed(url)) {
                    // URL already processed or determined to be a duplicate, skip it
                    // Send empty book and links to acknowledge processing
                    Book emptyBook;
                    std::vector<std::string> emptyLinks;
                    try {
                        if (!sendProcessedUrlToServer(serverSocket, url, emptyBook, emptyLinks)) {
                            log("Failed to send acknowledgment for skipped URL");
                            // Don't break - instead try to recover
                            std::this_thread::sleep_for(std::chrono::seconds(1));
                            continue;
                        }
                    } catch (const std::exception& e) {
                        log("Exception in sendProcessedUrlToServer for skipped URL: " + std::string(e.what()));
                        // Don't break - instead try to recover
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                        continue;
                    }
                    continue;
                }
                
                // Set a timeout for crawling a single page to avoid getting stuck
                const int MAX_CRAWL_TIME_SEC = 60; // 1 minute max per page
                auto crawlStartTime = std::chrono::steady_clock::now();
                
                // Crawl the page with timeout protection
                Book book;
                std::string html;
                std::thread crawlThread([&]() {
                    try {
                        auto result = crawl_page_with_html(hostname, url);
                        book = result.first;
                        html = result.second;
                    } catch (const std::exception& e) {
                        log("Exception in crawl_page_with_html: " + std::string(e.what()));
                        // Leave book and html empty to indicate error
                    }
                });
                
                // Wait for crawl thread with timeout
                if (crawlThread.joinable()) {
                    // Create a timeout thread
                    std::atomic<bool> crawlCompleted(false);
                    std::thread timeoutThread([&]() {
                        auto startTime = std::chrono::steady_clock::now();
                        while (!crawlCompleted.load()) {
                            auto now = std::chrono::steady_clock::now();
                            if (std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count() >= MAX_CRAWL_TIME_SEC) {
                                log("Crawl timeout for URL: " + url + " after " + std::to_string(MAX_CRAWL_TIME_SEC) + " seconds");
                                shouldStop.store(true); // Force worker to stop and restart
                                return;
                            }
                            std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        }
                    });
                    
                    // Join the crawl thread
                    crawlThread.join();
                    crawlCompleted.store(true);
                    if (timeoutThread.joinable()) {
                        timeoutThread.join();
                    }
                }
                
                // Check if crawl was successful (not empty and not timed out)
                if (shouldStop.load()) {
                    log("Stopping worker due to crawl timeout");
                    break;
                }
                
                // Even if HTML is empty, we need to send an acknowledgment
                std::vector<std::string> links;
                if (!html.empty()) {
                    try {
                        // Extract links from the HTML
                        links = find_all_links(html, hostname, url);
                    } catch (const std::exception& e) {
                        log("Exception in find_all_links: " + std::string(e.what()));
                        // Continue with empty links
                    }
                    
                    // Process the book if valid
                    if (!book.title.empty()) {
                        // Add the book to our local collection
                        books.push_back(book);
                        log("Found book: " + book.title + " (Price: " + book.price + ", Rating: " + book.rating + ")");
                    }
                } else {
                    log("Empty HTML response for URL: " + url + ", skipping link extraction");
                }
                
                // Send processed URL and extracted data back to server with retry logic
                int retries = 3;
                bool success = false;
                while (retries > 0 && !success && !shouldStop.load()) {
                    try {
                        success = sendProcessedUrlToServer(serverSocket, url, book, links);
                        if (!success) {
                            log("Failed to send processed URL to server, retries left: " + std::to_string(retries - 1));
                            retries--;
                            if (retries > 0) {
                                std::this_thread::sleep_for(std::chrono::seconds(1));
                            }
                        }
                    } catch (const std::exception& e) {
                        log("Exception in sendProcessedUrlToServer: " + std::string(e.what()));
                        retries--;
                        if (retries > 0) {
                            std::this_thread::sleep_for(std::chrono::seconds(1));
                        }
                    }
                }
                
                if (!success && !shouldStop.load()) {
                    log("Failed to send processed URL after retries, will continue with next URL");
                    // Don't break, try to continue
                }
                
                // Limit memory usage by processed URLs
                {
                    std::lock_guard<std::mutex> lock(processedUrlsMutex);
                    const size_t MAX_PROCESSED_URLS = 10000;
                    if (processedUrls.size() > MAX_PROCESSED_URLS) {
                        log("Clearing processed URLs cache (reached limit of " + std::to_string(MAX_PROCESSED_URLS) + ")");
                        processedUrls.clear();
                        // Keep only seed URL as processed if available
                        if (!startUrl.empty()) {
                            processedUrls.insert(startUrl);
                        }
                    }
                }
                
            } catch (const std::exception& e) {
                log("Exception in main loop: " + std::string(e.what()));
                // Sleep a bit to avoid tight loop in case of persistent errors
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        
        // Calculate total execution time
        auto endTime = std::chrono::high_resolution_clock::now();
        auto totalTime = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();
        
        log("Worker finished. Total execution time: " + std::to_string(totalTime) + " seconds");
        
        // Wait for reporter thread to finish
        if (reporterThread.joinable()) {
            reporterThread.join();
        }
        
        // Close the socket
        CLOSE_SOCKET(serverSocket);
        
        // If shutdown was requested by the server, exit the program
        if (shouldStop.load()) {
            log("Server requested shutdown. Worker will now exit.");
            // Wait briefly before exiting to allow for clean shutdown
            std::this_thread::sleep_for(std::chrono::seconds(1));
            break;
        }
        
        // Otherwise, wait a moment and try to reconnect
        log("Connection to server lost. Will attempt to reconnect in 5 seconds...");
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    
    return 0;
} 