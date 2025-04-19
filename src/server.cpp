/**
 * Web Scraper - Distributed Version
 * Central Server Implementation
 * 
 * This server listens for worker connections, assigns IDs, 
 * and tracks worker progress.
 */

#ifdef _MSC_VER
// Additional includes and defines for Visual Studio
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#ifndef _CRT_SECURE_NO_WARNINGS
#  define _CRT_SECURE_NO_WARNINGS
#endif
#else
#include <signal.h>
#endif


#include "../include/config.h"
#include "../include/compat.h"
#include <iostream>
#include <string>
#include <sstream>
#include <map>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <ctime>
#include <functional>
#include <iomanip> // For put_time
#include <cstring> // For memory functions
#include <atomic>
#include "../include/Book.h"
#include "../include/HtmlParser.h"
#include <queue>
#include <unordered_set>
#include <fstream>
#include <set>
#include "../include/Item.h"
#include <cmath>  // Add this include for log function

// Hide the std::log function from cmath to avoid conflicts
namespace std {
    using ::log;  // This line can be removed since we're including cmath
}

// Port to listen on
const int SERVER_PORT = 9000;
// Web interface port
const int WEB_PORT = 9001;

// Maximum number of pending connections in the queue
const int BACKLOG = 10;

// Buffer size for receiving data
const int BUFFER_SIZE = 1024;

// Flag to control crawler state
std::atomic<bool> crawlerEnabled(false);  // Start in stopped state

// Mutex for protecting the seed URL
std::mutex seedUrlMutex;
std::string currentSeedUrl = "https://books.toscrape.com/";  // Default seed URL

// Structure to hold worker information
struct WorkerInfo {
    int id;
    std::string address;
    int port;
    int pagesProcessed;
    std::chrono::time_point<std::chrono::system_clock> lastSeen;
    std::chrono::time_point<std::chrono::system_clock> startTime;
    int booksFound;
    int totalLinks;
    
    WorkerInfo() : id(0), port(0), pagesProcessed(0), booksFound(0), totalLinks(0) {
        startTime = std::chrono::system_clock::now();
        lastSeen = startTime;
    }
};

// Global flag to indicate server shutdown
std::atomic<bool> serverShutdown(false);
std::atomic<bool> shutdownRequested(false);

// Worker registry to track registered workers
class WorkerRegistry {
private:
    std::mutex mtx;
    std::map<int, WorkerInfo> workers;
    int nextWorkerId = 1;

public:
    int registerWorker(const std::string& address, int port) {
        std::lock_guard<std::mutex> lock(mtx);
        int id = nextWorkerId++;
        
        WorkerInfo worker;
        worker.id = id;
        worker.address = address;
        worker.port = port;
        worker.pagesProcessed = 0;
        worker.booksFound = 0;
        worker.totalLinks = 0;
        worker.startTime = std::chrono::system_clock::now();
        worker.lastSeen = worker.startTime;
        
        workers[id] = worker;
        
        return id;
    }
    
    void updateProgress(int workerId, int pagesProcessed) {
        std::lock_guard<std::mutex> lock(mtx);
        if (workers.find(workerId) != workers.end()) {
            workers[workerId].pagesProcessed = pagesProcessed;
            workers[workerId].lastSeen = std::chrono::system_clock::now();
        }
    }
    
    void disconnectWorker(int workerId) {
        std::lock_guard<std::mutex> lock(mtx);
        workers.erase(workerId);
    }
    
    int getTotalPagesProcessed() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mtx));
        int total = 0;
        for (const auto& pair : workers) {
            total += pair.second.pagesProcessed;
        }
        return total;
    }
    
    int getActiveWorkerCount() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mtx));
        return workers.size();
    }
    
    std::vector<WorkerInfo> getAllWorkers() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mtx));
        std::vector<WorkerInfo> result;
        for (const auto& pair : workers) {
            result.push_back(pair.second);
        }
        return result;
    }
    
    std::vector<int> getAllWorkerIds() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mtx));
        std::vector<int> ids;
        for (const auto& pair : workers) {
            ids.push_back(pair.first);
        }
        return ids;
    }
    
    void incrementProcessedCount(int workerId) {
        std::lock_guard<std::mutex> lock(mtx);
        if (workers.find(workerId) != workers.end()) {
            workers[workerId].pagesProcessed++;
            workers[workerId].lastSeen = std::chrono::system_clock::now();
        }
    }
    
    void updateWorkerStats(int workerId, int addedLinks, bool foundBook) {
        std::lock_guard<std::mutex> lock(mtx);
        if (workers.find(workerId) != workers.end()) {
            workers[workerId].lastSeen = std::chrono::system_clock::now();
            workers[workerId].totalLinks += addedLinks;
            if (foundBook) {
                workers[workerId].booksFound++;
            }
        }
    }
};

// Global worker registry
WorkerRegistry workerRegistry;

// Function to get current timestamp as string
std::string getTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_time), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// Log with timestamp
void logMessage(const std::string& message) {
    std::cout << "[" << getTimestamp() << "] " << message << std::endl;
}

// Helper functions for URL handling - defined as inline to avoid duplicate symbol errors
#ifdef SERVER_DEFINE_URL_HELPERS
// Using these server-specific helper functions to avoid conflicts
inline std::string server_canonicalize_url(const std::string& url) {
    // Remove trailing slash
    std::string canonicalUrl = url;
    if (!canonicalUrl.empty() && canonicalUrl.back() == '/') {
        canonicalUrl.pop_back();
    }
    
    // Convert to lowercase
    std::transform(canonicalUrl.begin(), canonicalUrl.end(), canonicalUrl.begin(),
                  [](unsigned char c) { return std::tolower(c); });
    
    return canonicalUrl;
}

inline bool server_is_book_page(const std::string& url) {
    // Simple check - book pages typically contain "/catalogue/" and end with ".html"
    return url.find("/catalogue/") != std::string::npos && 
           url.find("page-") == std::string::npos && 
           url.find("index.html") == std::string::npos &&
           url.find("category/") == std::string::npos &&
           url.find(".html") != std::string::npos;
}

inline bool server_is_category_page(const std::string& url) {
    // Category pages contain "/catalogue/" and either "page-" or "category/" or "index.html"
    return url.find("/catalogue/") != std::string::npos && 
           (url.find("page-") != std::string::npos || 
            url.find("category/") != std::string::npos || 
            url.find("index.html") != std::string::npos);
}
#else
// Using existing canonicalize_url functions from HtmlParser.cpp
#define server_canonicalize_url canonicalize_url
#define server_is_book_page is_book_page
#define server_is_category_page is_category_page
#endif // SERVER_DEFINE_URL_HELPERS

// URL Queue Manager
class UrlQueueManager {
private:
    std::queue<std::string> urlQueue;
    std::set<std::string> processedUrls;
    std::set<std::string> queuedUrls;
    std::map<std::string, int> assignedUrls; // Maps URLs to worker IDs they're assigned to
    std::mutex queueMutex;
    std::string hostname;
    std::vector<Book> collectedBooks; // Keep for backward compatibility
    std::vector<Item> collectedItems; // New generic items collection
    std::string startUrl;
    ItemType currentItemType = ItemType::BOOK; // Default to book type

public:
    UrlQueueManager(const std::string& host = "books.toscrape.com", const std::string& start = "https://books.toscrape.com/") 
    : hostname(host), startUrl(start) {
        // Determine the type based on the hostname
        if (host.find("toscrape.com") != std::string::npos) {
            currentItemType = ItemType::BOOK;
        } else if (host.find("indeed.com") != std::string::npos || 
                  host.find("linkedin.com/jobs") != std::string::npos ||
                  host.find("monster.com") != std::string::npos) {
            currentItemType = ItemType::JOB;
        } else if (host.find("amazon.com") != std::string::npos || 
                  host.find("ebay.com") != std::string::npos ||
                  host.find("walmart.com") != std::string::npos) {
            currentItemType = ItemType::PRODUCT;
        } else if (host.find("medium.com") != std::string::npos ||
                  host.find("news.") != std::string::npos ||
                  host.find("blog.") != std::string::npos) {
            currentItemType = ItemType::ARTICLE;
        } else {
            currentItemType = ItemType::GENERIC;
        }
    }
    
    void setSeedUrl(const std::string& url) {
        std::lock_guard<std::mutex> lock(queueMutex);
        
        // Update the start URL
        startUrl = url;
        
        // Determine domain from URL
        size_t protocolEnd = url.find("://");
        size_t domainStart = protocolEnd != std::string::npos ? protocolEnd + 3 : 0;
        size_t domainEnd = url.find("/", domainStart);
        hostname = domainEnd != std::string::npos ? 
            url.substr(domainStart, domainEnd - domainStart) : 
            url.substr(domainStart);
        
        // Determine item type based on hostname
        if (hostname.find("toscrape.com") != std::string::npos) {
            currentItemType = ItemType::BOOK;
        } else if (hostname.find("indeed.com") != std::string::npos || 
                  hostname.find("linkedin.com") != std::string::npos ||
                  hostname.find("monster.com") != std::string::npos) {
            currentItemType = ItemType::JOB;
        } else if (hostname.find("amazon.com") != std::string::npos || 
                  hostname.find("ebay.com") != std::string::npos ||
                  hostname.find("walmart.com") != std::string::npos) {
            currentItemType = ItemType::PRODUCT;
        } else if (hostname.find("medium.com") != std::string::npos ||
                  hostname.find("news.") != std::string::npos ||
                  hostname.find("blog.") != std::string::npos) {
            currentItemType = ItemType::ARTICLE;
        } else {
            currentItemType = ItemType::GENERIC;
        }
        
        // Reset queue and processed URLs
        while (!urlQueue.empty()) {
            urlQueue.pop();
        }
        queuedUrls.clear();
        processedUrls.clear();
        assignedUrls.clear();
        collectedBooks.clear();
        collectedItems.clear();
        
        // We don't add the URL to the queue here anymore - that will happen when the crawler starts
        
        logMessage("Seed URL set to: " + url + " (Item type: " + getItemTypeString() + ")");
    }
    
    std::string getSeedUrl() {
        std::lock_guard<std::mutex> lock(queueMutex);
        return startUrl;
    }
    
    ItemType getCurrentItemType() {
        std::lock_guard<std::mutex> lock(queueMutex);
        return currentItemType;
    }
    
    std::string getItemTypeString() {
        switch (currentItemType) {
            case ItemType::BOOK: return "Book";
            case ItemType::JOB: return "Job";
            case ItemType::PRODUCT: return "Product";
            case ItemType::ARTICLE: return "Article";
            default: return "Generic";
        }
    }
    
    void addUrl(const std::string& url) {
        std::lock_guard<std::mutex> lock(queueMutex);
        
        // Skip if already processed or queued
        std::string canonical = server_canonicalize_url(url);
        
        if (processedUrls.find(canonical) != processedUrls.end()) {
            // Skip URLs that are already processed
            return;
        }
        
        if (queuedUrls.find(canonical) != queuedUrls.end()) {
            // Skip URLs that are already in the queue
            return;
        }
        
        // Only add URLs for the same domain
        if (url.find(hostname) == std::string::npos) {
            return;
        }
        
        // Add to the queue
        urlQueue.push(url);
        queuedUrls.insert(canonical);
        
        logMessage("Added URL to queue: " + url);
    }
    
    void addUrls(const std::vector<std::string>& urls) {
        int addedCount = 0;
        int skippedCount = 0;
        
        for (const auto& url : urls) {
            // Check if skippable before locking
            std::string canonical = server_canonicalize_url(url);
            
            std::lock_guard<std::mutex> lock(queueMutex);
            
            // Skip if already processed or queued
            if (processedUrls.find(canonical) != processedUrls.end() ||
                queuedUrls.find(canonical) != queuedUrls.end()) {
                skippedCount++;
                continue;
            }
            
            // Only add URLs for the same domain
            if (url.find(hostname) == std::string::npos) {
                skippedCount++;
                continue;
            }
            
            // Add to the queue
            urlQueue.push(url);
            queuedUrls.insert(canonical);
            addedCount++;
        }
        
        if (addedCount > 0 || skippedCount > 0) {
            logMessage("Batch URL add: " + std::to_string(addedCount) + 
                       " added, " + std::to_string(skippedCount) + " skipped");
        }
    }
    
    bool getNextUrl(std::string& url, int workerId = -1) {
        std::lock_guard<std::mutex> lock(queueMutex);
        
        // Check if the queue is empty
        if (urlQueue.empty()) {
            // No URLs available in the queue
            return false;
        }
        
        // Get the next URL from the queue
        url = urlQueue.front();
        urlQueue.pop();
        
        // Log that we're getting a URL
        logMessage("Getting next URL for worker " + std::to_string(workerId) + ": " + url);
        
        // Remove from queued URLs list
        std::string canonical = server_canonicalize_url(url);
        queuedUrls.erase(canonical);
        
        // If this is being assigned to a worker, track it
        if (workerId != -1) {
            assignedUrls[url] = workerId;
        }
        
        return true;
    }
    
    void markProcessed(const std::string& url) {
        std::lock_guard<std::mutex> lock(queueMutex);
        
        // Get canonical URL
        std::string canonical = server_canonicalize_url(url);
        
        // Check if it's already processed (shouldn't happen, but possible with multiple workers)
        if (processedUrls.find(canonical) != processedUrls.end()) {
            logMessage("Warning: URL was already marked as processed: " + url);
            return;
        }
        
        // Add to processed URLs
        processedUrls.insert(canonical);
        
        // Remove from queued URLs if it's there
        if (queuedUrls.find(canonical) != queuedUrls.end()) {
            queuedUrls.erase(canonical);
        }
        
        // Remove from assigned URLs
        auto it = assignedUrls.find(url);
        if (it != assignedUrls.end()) {
            int workerIdForUrl = it->second;
            assignedUrls.erase(it);
            logMessage("URL processed by worker " + std::to_string(workerIdForUrl) + ": " + url);
        } else {
            logMessage("URL processed but wasn't assigned to any worker: " + url);
        }
    }
    
    bool isUrlProcessed(const std::string& url) {
        std::lock_guard<std::mutex> lock(queueMutex);
        return processedUrls.find(server_canonicalize_url(url)) != processedUrls.end();
    }
    
    bool isUrlQueued(const std::string& url) {
        std::lock_guard<std::mutex> lock(queueMutex);
        return queuedUrls.find(server_canonicalize_url(url)) != queuedUrls.end();
    }
    
    void reassignUrlsFromWorker(int workerId) {
        std::lock_guard<std::mutex> lock(queueMutex);
        
        // Find URLs assigned to this worker
        std::vector<std::string> urlsToReassign;
        for (const auto& pair : assignedUrls) {
            if (pair.second == workerId) {
                urlsToReassign.push_back(pair.first);
            }
        }
        
        // Remove from assigned and readd to queue
        for (const auto& url : urlsToReassign) {
            assignedUrls.erase(url);
            urlQueue.push(url);
        }
        
        logMessage("Reassigned " + std::to_string(urlsToReassign.size()) + 
                  " URLs from disconnected worker " + std::to_string(workerId));
    }
    
    size_t getQueueSize() {
        std::lock_guard<std::mutex> lock(queueMutex);
        return urlQueue.size();
    }
    
    size_t getProcessedCount() {
        std::lock_guard<std::mutex> lock(queueMutex);
        return processedUrls.size();
    }
    
    int getBookCount() {
        std::lock_guard<std::mutex> lock(queueMutex);
        return collectedBooks.size();
    }
    
    int getItemCount() {
        std::lock_guard<std::mutex> lock(queueMutex);
        return collectedItems.size();
    }
    
    std::vector<Item> getCollectedItems() {
        std::lock_guard<std::mutex> lock(queueMutex);
        return collectedItems;
    }
    
    void addBook(const Book& book) {
        std::lock_guard<std::mutex> lock(queueMutex);
        
        // Check if this book is already in the collection, but use title + price + rating instead of URL
        auto it = std::find_if(collectedBooks.begin(), collectedBooks.end(),
                              [&book](const Book& b) { 
                                  return b.title == book.title && 
                                         b.price == book.price && 
                                         b.rating == book.rating; 
                              });
        
        if (it == collectedBooks.end()) {
            // Add the book if it's not already in the collection
            collectedBooks.push_back(book);
            
            // Also add to the generic items collection
            collectedItems.push_back(Item::fromBook(book));
            
            logMessage("Added book: " + book.title);
        } else {
            // Book already exists with different URL, log it
            logMessage("Skipped duplicate book (different URL): " + book.title);
        }
    }
    
    void addItem(const Item& item) {
        std::lock_guard<std::mutex> lock(queueMutex);
        
        // Check if this item is already in the collection by comparing title, price, and rating
        auto it = std::find_if(collectedItems.begin(), collectedItems.end(),
                              [&item](const Item& i) { 
                                  return i.type == item.type && 
                                         i.title == item.title && 
                                         i.price == item.price && 
                                         i.rating == item.rating; 
                              });
        
        if (it == collectedItems.end()) {
            // Add the item if it's not already in the collection
            collectedItems.push_back(item);
            
            // If it's a book, also add to the books collection for backward compatibility
            if (item.type == ItemType::BOOK) {
                Book book;
                book.title = item.title;
                book.url = item.url;
                book.price = item.fields.count("price_original") ? 
                            item.fields.at("price_original") : std::to_string(item.price);
                book.rating = item.fields.count("rating_original") ? 
                             item.fields.at("rating_original") : std::to_string(item.rating);
                collectedBooks.push_back(book);
            }
            
            logMessage("Added " + item.typeToString() + ": " + item.title);
        } else {
            // Item already exists, log it
            logMessage("Skipped duplicate " + item.typeToString() + " (different URL): " + item.title);
        }
    }
    
    void saveCollectedBooks(const std::string& filename) {
        std::lock_guard<std::mutex> lock(queueMutex);
        
        try {
            // Open the file for writing
            std::ofstream outfile(filename);
            if (!outfile.is_open()) {
                logMessage("Error: Unable to open file for writing: " + filename);
                return;
            }
            
            // Write the header
            outfile << "Title,Price,Rating,URL\n";
            
            // Write each book
            for (const auto& book : collectedBooks) {
                outfile << "\"" << book.title << "\",";
                outfile << "\"" << book.price << "\",";
                outfile << "\"" << book.rating << "\",";
                outfile << "\"" << book.url << "\"\n";
            }
            
            outfile.close();
            logMessage("Saved " + std::to_string(collectedBooks.size()) + " books to " + filename);
        } catch (const std::exception& e) {
            logMessage("Error saving books: " + std::string(e.what()));
        }
    }
    
    void saveCollectedItems(const std::string& filename) {
        std::lock_guard<std::mutex> lock(queueMutex);
        
        try {
            // Open the file for writing
            std::ofstream outfile(filename);
            if (!outfile.is_open()) {
                logMessage("Error: Unable to open file for writing: " + filename);
                return;
            }
            
            // Write the header - common fields first, then type-specific fields
            outfile << "Type,Title,Price,Rating,Category,URL,Description";
            
            // Add type-specific headers
            bool hasJobs = false, hasProducts = false, hasArticles = false;
            
            for (const auto& item : collectedItems) {
                if (item.type == ItemType::JOB) hasJobs = true;
                else if (item.type == ItemType::PRODUCT) hasProducts = true;
                else if (item.type == ItemType::ARTICLE) hasArticles = true;
            }
            
            if (hasJobs) outfile << ",Company,Location,Salary";
            if (hasProducts) outfile << ",ImageUrl";
            if (hasArticles) outfile << ",PublishDate,Author";
            
            outfile << "\n";
            
            // Write each item
            for (const auto& item : collectedItems) {
                outfile << "\"" << item.typeToString() << "\",";
                outfile << "\"" << item.title << "\",";
                outfile << "\"" << item.price << "\",";
                outfile << "\"" << item.rating << "\",";
                outfile << "\"" << item.category << "\",";
                outfile << "\"" << item.url << "\",";
                outfile << "\"" << item.description << "\"";
                
                // Add type-specific fields
                if (hasJobs) {
                    outfile << ",\"" << (item.fields.count("company") ? item.fields.at("company") : "") << "\"";
                    outfile << ",\"" << (item.fields.count("location") ? item.fields.at("location") : "") << "\"";
                    outfile << ",\"" << (item.fields.count("salary") ? item.fields.at("salary") : "") << "\"";
                }
                
                if (hasProducts) {
                    outfile << ",\"" << item.imageUrl << "\"";
                }
                
                if (hasArticles) {
                    outfile << ",\"" << item.date << "\"";
                    outfile << ",\"" << (item.fields.count("author") ? item.fields.at("author") : "") << "\"";
                }
                
                outfile << "\n";
            }
            
            outfile.close();
            logMessage("Saved " + std::to_string(collectedItems.size()) + " items to " + filename);
        } catch (const std::exception& e) {
            logMessage("Error saving items: " + std::string(e.what()));
        }
    }
    
    // Get item statistics (count by type, average price, etc.)
    std::map<std::string, std::string> getItemStats() {
        std::lock_guard<std::mutex> lock(queueMutex);
        std::map<std::string, std::string> stats;
        
        // Count items by type
        int bookCount = 0, jobCount = 0, productCount = 0, articleCount = 0, genericCount = 0;
        double totalPrice = 0.0, bookTotalPrice = 0.0, jobTotalPrice = 0.0, productTotalPrice = 0.0;
        int ratedItems = 0;
        double totalRating = 0.0;
        std::set<std::string> categories;
        
        for (const auto& item : collectedItems) {
            // Count by type
            switch (item.type) {
                case ItemType::BOOK: bookCount++; 
                    if (item.price > 0) bookTotalPrice += item.price; 
                    break;
                case ItemType::JOB: jobCount++; 
                    if (item.price > 0) jobTotalPrice += item.price; 
                    break;
                case ItemType::PRODUCT: productCount++; 
                    if (item.price > 0) productTotalPrice += item.price; 
                    break;
                case ItemType::ARTICLE: articleCount++; break;
                default: genericCount++; break;
            }
            
            // Track price and rating
            if (item.price > 0) totalPrice += item.price;
            if (item.rating > 0) {
                totalRating += item.rating;
                ratedItems++;
            }
            
            // Track categories
            if (!item.category.empty()) {
                categories.insert(item.category);
            }
        }
        
        // Store stats
        stats["totalItems"] = std::to_string(collectedItems.size());
        stats["bookCount"] = std::to_string(bookCount);
        stats["jobCount"] = std::to_string(jobCount);
        stats["productCount"] = std::to_string(productCount);
        stats["articleCount"] = std::to_string(articleCount);
        stats["genericCount"] = std::to_string(genericCount);
        
        // Average prices
        if (collectedItems.size() > 0 && totalPrice > 0) {
            stats["avgPrice"] = std::to_string(totalPrice / collectedItems.size());
        } else {
            stats["avgPrice"] = "0";
        }
        
        if (bookCount > 0 && bookTotalPrice > 0) {
            stats["avgBookPrice"] = std::to_string(bookTotalPrice / bookCount);
        } else {
            stats["avgBookPrice"] = "0";
        }
        
        if (jobCount > 0 && jobTotalPrice > 0) {
            stats["avgJobSalary"] = std::to_string(jobTotalPrice / jobCount);
        } else {
            stats["avgJobSalary"] = "0";
        }
        
        if (productCount > 0 && productTotalPrice > 0) {
            stats["avgProductPrice"] = std::to_string(productTotalPrice / productCount);
        } else {
            stats["avgProductPrice"] = "0";
        }
        
        // Average rating
        if (ratedItems > 0) {
            stats["avgRating"] = std::to_string(totalRating / ratedItems);
        } else {
            stats["avgRating"] = "0";
        }
        
        // Categories
        stats["categoryCount"] = std::to_string(categories.size());
        
        std::string categoryList = "";
        for (const auto& cat : categories) {
            if (!categoryList.empty()) categoryList += ", ";
            categoryList += cat;
        }
        stats["categories"] = categoryList;
        
        return stats;
    }
    
    void resetCollectedData() {
        std::lock_guard<std::mutex> lock(queueMutex);
        
        // Always clear the URL queue first
        while (!urlQueue.empty()) {
            urlQueue.pop();
        }
        
        // Reset all collections completely
        processedUrls.clear();
        queuedUrls.clear();
        assignedUrls.clear();
        collectedBooks.clear();
        collectedItems.clear();
        
        // Log that we're starting fresh
        logMessage("Collected data reset. Seed URL will be crawled on restart.");
    }
    
    void addSeedUrl(const std::string& url) {
        std::lock_guard<std::mutex> lock(queueMutex);
        
        // Get canonical form of URL
        std::string canonical = server_canonicalize_url(url);
        
        // Always remove from processed URLs to ensure the seed URL can be processed again
        if (processedUrls.find(canonical) != processedUrls.end()) {
            processedUrls.erase(canonical);
            logMessage("Removed seed URL from processed list to allow re-crawling: " + url);
        }
        
        // Remove from queued URLs if it's already there (to avoid duplicates)
        if (queuedUrls.find(canonical) != queuedUrls.end()) {
            queuedUrls.erase(canonical);
            logMessage("Removed seed URL from queued list to avoid duplication.");
        }
        
        // Add to queue (it should be fresh now)
        urlQueue.push(url);
        queuedUrls.insert(canonical);
        logMessage("Added seed URL to queue: " + url);
    }
};

// Global URL queue manager
UrlQueueManager* urlQueueManager = nullptr;

// Initialize URL queue manager with default values
void initUrlQueueManager() {
    if (urlQueueManager == nullptr) {
        urlQueueManager = new UrlQueueManager();
    }
}

// Handle client connection in a separate thread
void handleClient(SOCKET clientSocket, const std::string& clientAddress, int clientPort) {
    char buffer[BUFFER_SIZE];
    int bytesReceived;
    int workerId = -1;
    bool registered = false;
    
    logMessage("New connection from " + clientAddress + ":" + std::to_string(clientPort));
    
    // Receive messages from the client
    while (!serverShutdown.load() && (bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        // Null-terminate the received data
        buffer[bytesReceived] = '\0';
        
        // Process the message
        std::string message(buffer);
        
        if (message.find("REGISTER") == 0) {
            // Handle registration
            workerId = workerRegistry.registerWorker(clientAddress, clientPort);
            registered = true;
            
            // Send back the assigned ID
            std::string response = "ASSIGN_ID:" + std::to_string(workerId);
            send(clientSocket, response.c_str(), response.length(), 0);
            
            logMessage("Registered worker " + std::to_string(workerId) + " from " + 
                clientAddress + ":" + std::to_string(clientPort));
        }
        else if (message.find("GET_URL") == 0 && registered) {
            // Only process URL requests if crawler is enabled
            if (crawlerEnabled.load()) {
                // Handle URL request
                std::string nextUrl;
                bool hasUrl = urlQueueManager->getNextUrl(nextUrl, workerId);
                
                std::string response;
                if (hasUrl) {
                    response = "URL:" + nextUrl;
                    
                    // Track that we sent this URL to this worker
                    logMessage("Sent URL to worker " + std::to_string(workerId) + ": " + nextUrl);
                    
                    // Add small delay to ensure message synchronization
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                } else if (serverShutdown.load()) {
                    response = "SHUTDOWN";
                } else {
                    // No URLs available at the moment
                    response = "WAIT";
                }
                
                send(clientSocket, response.c_str(), response.length(), 0);
            } else {
                // If crawler is not enabled, tell worker to wait
                std::string response = "WAIT";
                send(clientSocket, response.c_str(), response.length(), 0);
                
                // Add a longer delay when crawler is disabled
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }
        else if (message.find("PROCESSED:") == 0 && registered) {
            // Only process URL updates if crawler is enabled
            if (crawlerEnabled.load()) {
                // Handle processed URL update
                std::string data = message.substr(10); // Skip "PROCESSED:"
                
                // Parse the JSON-like data
                // Format: {url:"...",book:{title:"...",price:"...",rating:"...",url:"..."}}
                
                // Extract the URL
                size_t urlStart = data.find("url:\"") + 5;
                size_t urlEnd = data.find("\"", urlStart);
                std::string url = data.substr(urlStart, urlEnd - urlStart);
                
                // Mark URL as processed
                urlQueueManager->markProcessed(url);
                
                // Increment the worker's processed count
                workerRegistry.incrementProcessedCount(workerId);
                
                // Extract book data
                Book book;
                
                size_t titleStart = data.find("title:\"") + 7;
                size_t titleEnd = data.find("\"", titleStart);
                book.title = data.substr(titleStart, titleEnd - titleStart);
                
                size_t priceStart = data.find("price:\"") + 7;
                size_t priceEnd = data.find("\"", priceStart);
                book.price = data.substr(priceStart, priceEnd - priceStart);
                
                size_t ratingStart = data.find("rating:\"") + 8;
                size_t ratingEnd = data.find("\"", ratingStart);
                book.rating = data.substr(ratingStart, ratingEnd - ratingStart);
                
                size_t bookUrlStart = data.find("url:\"", ratingEnd) + 5;
                size_t bookUrlEnd = data.find("\"", bookUrlStart);
                book.url = data.substr(bookUrlStart, bookUrlEnd - bookUrlStart);
                
                // Add book to collection if valid
                if (!book.title.empty()) {
                    urlQueueManager->addBook(book);
                }
                
                logMessage("Worker " + std::to_string(workerId) + " processed URL: " + url);
                
                // Send ACK for the PROCESSED message
                std::string ackResponse = "ACK";
                send(clientSocket, ackResponse.c_str(), ackResponse.length(), 0);
            } else {
                // If crawler is not enabled, respond with ACK anyway
                std::string response = "ACK";
                send(clientSocket, response.c_str(), response.length(), 0);
            }
        }
        else if (message.find("LINKS:") == 0 && registered) {
            // Only process URL updates if crawler is enabled
            if (crawlerEnabled.load()) {
                // Handle links batch
                // Format: "LINKS:1/3:{url:"...",links:["...", "..."]}"
                
                // Extract the batch info
                size_t batchInfoEnd = message.find(":{");
                if (batchInfoEnd == std::string::npos) {
                    logMessage("Invalid LINKS message format from worker " + std::to_string(workerId));
                    std::string response = "ACK";
                    send(clientSocket, response.c_str(), response.length(), 0);
                    continue;
                }
                
                // Extract the data part
                std::string data = message.substr(batchInfoEnd + 1);
                
                // Extract the URL
                size_t urlStart = data.find("url:\"") + 5;
                size_t urlEnd = data.find("\"", urlStart);
                std::string url = data.substr(urlStart, urlEnd - urlStart);
                
                // Extract links
                std::vector<std::string> links;
                size_t linksStart = data.find("links:[") + 7;
                size_t linksEnd = data.find("]", linksStart);
                std::string linksStr = data.substr(linksStart, linksEnd - linksStart);
                
                // Parse the links array
                size_t pos = 0;
                while ((pos = linksStr.find("\"", pos)) != std::string::npos) {
                    size_t endPos = linksStr.find("\"", pos + 1);
                    if (endPos == std::string::npos) break;
                    
                    std::string link = linksStr.substr(pos + 1, endPos - pos - 1);
                    links.push_back(link);
                    pos = endPos + 1;
                }
                
                // Add links to the queue
                urlQueueManager->addUrls(links);
                
                // Update worker stats (links added)
                workerRegistry.updateWorkerStats(workerId, links.size(), false);
                
                logMessage("Worker " + std::to_string(workerId) + " sent " + 
                           std::to_string(links.size()) + " links for URL: " + url);
                
                // Send ACK for the LINKS message
                std::string response = "ACK";
                send(clientSocket, response.c_str(), response.length(), 0);
            } else {
                // If crawler is not enabled, respond with ACK anyway
                std::string response = "ACK";
                send(clientSocket, response.c_str(), response.length(), 0);
            }
        }
        else if (message.find("PROGRESS:") == 0 && registered) {
            // Handle progress update (always respond with ACK, even if crawler is disabled)
            // Extract progress count
            int progress = 0;
            try {
                progress = std::stoi(message.substr(9)); // Skip "PROGRESS:"
            } catch (const std::exception& e) {
                logMessage("Invalid progress update format from worker " + std::to_string(workerId));
            }
            
            // Update worker stats
            workerRegistry.updateProgress(workerId, progress);
            
            // Always respond with ACK to keep the worker alive
            std::string response = "ACK";
            send(clientSocket, response.c_str(), response.length(), 0);
            
            logMessage("Worker " + std::to_string(workerId) + " progress update: " + 
                       std::to_string(progress) + " pages processed");
        }
        else {
            // Unknown message
            logMessage("Received unknown message from worker " + std::to_string(workerId) + 
                ": " + message);
        }
        
        // If server is shutting down, notify the worker
        if (serverShutdown.load()) {
            std::string shutdownMsg = "SHUTDOWN";
            send(clientSocket, shutdownMsg.c_str(), shutdownMsg.length(), 0);
            break;
        }
    }
    
    // Handle disconnection
    if (workerId != -1) {
        workerRegistry.disconnectWorker(workerId);
        logMessage("Worker " + std::to_string(workerId) + " disconnected");
    }
    
    // Close the client socket
    CLOSE_SOCKET(clientSocket);
}

// Function to display server status periodically
void displayStatus() {
    while (!serverShutdown.load()) {
        if (urlQueueManager == nullptr) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }
        
        // Display status information
        logMessage("======== Server Status ========");
        logMessage("Active workers: " + std::to_string(workerRegistry.getActiveWorkerCount()));
        logMessage("Total pages processed: " + std::to_string(workerRegistry.getTotalPagesProcessed()));
        logMessage("Pending URLs: " + std::to_string(urlQueueManager->getQueueSize()));
        logMessage("Processed URLs: " + std::to_string(urlQueueManager->getProcessedCount()));
        logMessage("Collected books: " + std::to_string(urlQueueManager->getBookCount()));
        
        // Get all workers and display their individual stats
        std::vector<WorkerInfo> workers = workerRegistry.getAllWorkers();
        if (!workers.empty()) {
            logMessage("------ Worker Statistics ------");
            for (const auto& worker : workers) {
                auto lastSeen = std::chrono::system_clock::to_time_t(worker.lastSeen);
                std::string lastSeenStr;
                {
                    std::ostringstream ss;
                    ss << std::put_time(std::localtime(&lastSeen), "%H:%M:%S");
                    lastSeenStr = ss.str();
                }
                
                // Calculate worker uptime
                auto now = std::chrono::system_clock::now();
                auto uptime = std::chrono::duration_cast<std::chrono::minutes>(now - worker.startTime).count();
                
                // Calculate pages per minute
                double pagesPerMinute = 0;
                if (uptime > 0) {
                    pagesPerMinute = static_cast<double>(worker.pagesProcessed) / uptime;
                }
                
                logMessage("Worker " + std::to_string(worker.id) + " (" + 
                           worker.address + ":" + std::to_string(worker.port) + "): " +
                           std::to_string(worker.pagesProcessed) + " pages, " +
                           std::to_string(worker.booksFound) + " books, " +
                           std::to_string(worker.totalLinks) + " links, " +
                           "uptime: " + std::to_string(uptime) + " min, " +
                           "rate: " + std::to_string(pagesPerMinute) + " pages/min, " +
                           "last seen: " + lastSeenStr);
            }
        }
        
        logMessage("==============================");
        
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

// Signal handler function
void signalHandler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        logMessage("Received shutdown signal. Press Ctrl+C again to force shutdown...");
        if (shutdownRequested.load()) {
            logMessage("Force shutdown initiated...");
            exit(0);
        }
        shutdownRequested.store(true);
        serverShutdown.store(true);
    }
}

// Thread function to check for shutdown request
void checkShutdown() {
    logMessage("Server is running. Press Ctrl+C to shutdown...");
    
    while (!serverShutdown.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    // Only proceed with shutdown if it was requested
    if (shutdownRequested.load()) {
        logMessage("Shutdown initiated. Waiting for workers to terminate...");
        
        // Save collected data
        if (urlQueueManager != nullptr) {
            logMessage("Saving collected data...");
            urlQueueManager->saveCollectedBooks("books.csv");
            urlQueueManager->saveCollectedItems("items.csv");
            logMessage("Data has been saved.");
        } else {
            logMessage("WARNING: URL queue manager was null, could not save data!");
        }
        
        // Give workers time to receive shutdown signal
        std::this_thread::sleep_for(std::chrono::seconds(3));
        
        logMessage("Server shutdown complete.");
    }
}

// Broadcast shutdown to all connected workers
void broadcastShutdown() {
    // Get all worker IDs
    std::vector<int> workerIds = workerRegistry.getAllWorkerIds();
    
    logMessage("Broadcasting shutdown to " + std::to_string(workerIds.size()) + " workers...");
    
    // The actual broadcast happens in handleClient threads
    // through the serverShutdown flag
}

// Class to handle HTTP API requests
class ApiHandler {
private:
    SOCKET apiSocket;
    std::atomic<bool> running;
    std::thread serverThread;
    
public:
    ApiHandler() : apiSocket(INVALID_SOCKET), running(false) {}
    
    ~ApiHandler() {
        stop();
    }
    
    void start() {
        if (running.load()) {
            return;
        }
        
        running.store(true);
        serverThread = std::thread(&ApiHandler::runServer, this);
    }
    
    void stop() {
        if (!running.load()) {
            return;
        }
        
        running.store(false);
        
        if (apiSocket != INVALID_SOCKET) {
            CLOSE_SOCKET(apiSocket);
            apiSocket = INVALID_SOCKET;
        }
        
        if (serverThread.joinable()) {
            serverThread.join();
        }
    }
    
private:
    void runServer() {
        // Create socket
        apiSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (apiSocket == INVALID_SOCKET) {
            logMessage("Error creating API socket");
            return;
        }
        
        // Set socket to reuse address option
        int opt = 1;
        if (setsockopt(apiSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) == SOCKET_ERROR) {
            logMessage("Error setting socket options for API");
            CLOSE_SOCKET(apiSocket);
            return;
        }
        
        // Set up server address
        struct sockaddr_in serverAddr;
        ZeroMemory(&serverAddr, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(WEB_PORT);
        
        // Bind socket
        if (bind(apiSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            logMessage("Error binding API socket");
            CLOSE_SOCKET(apiSocket);
            return;
        }
        
        // Listen for connections
        if (listen(apiSocket, BACKLOG) == SOCKET_ERROR) {
            logMessage("Error listening on API socket");
            CLOSE_SOCKET(apiSocket);
            return;
        }
        
        logMessage("API server listening on port " + std::to_string(WEB_PORT));
        
        // Set up select() for non-blocking accept
        fd_set readfds;
        struct timeval timeout;
        
        // Main accept loop
        while (running.load() && !serverShutdown.load()) {
            FD_ZERO(&readfds);
            FD_SET(apiSocket, &readfds);
            
            // Set timeout to 1 second
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            
            // Wait for activity on the socket
            int activity = select(apiSocket + 1, &readfds, NULL, NULL, &timeout);
            
            if (activity == SOCKET_ERROR) {
                logMessage("Select error in API server");
                break;
            }
            
            // Check if we have a connection
            if (activity > 0 && FD_ISSET(apiSocket, &readfds)) {
                struct sockaddr_in clientAddr;
                socklen_t clientAddrSize = sizeof(clientAddr);
                
                SOCKET clientSocket = accept(apiSocket, (struct sockaddr*)&clientAddr, &clientAddrSize);
                if (clientSocket == INVALID_SOCKET) {
                    logMessage("Error accepting API connection");
                    continue;
                }
                
                // Handle API request in a new thread
                std::thread clientThread(&ApiHandler::handleRequest, this, clientSocket);
                clientThread.detach();
            }
        }
        
        // Clean up
        if (apiSocket != INVALID_SOCKET) {
            CLOSE_SOCKET(apiSocket);
            apiSocket = INVALID_SOCKET;
        }
        
        logMessage("API server stopped");
    }
    
    void handleRequest(SOCKET clientSocket) {
        // Buffer for receiving data
        char buffer[BUFFER_SIZE];
        
        // Receive data
        int bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);
        if (bytesReceived <= 0) {
            CLOSE_SOCKET(clientSocket);
            return;
        }
        
        // Null-terminate the buffer
        buffer[bytesReceived] = '\0';
        
        // Parse the HTTP request
        std::string request(buffer);
        std::string response;
        std::string contentType = "text/plain";
        int statusCode = 200;
        
        if (request.find("GET /api/status") != std::string::npos) {
            // Return crawler status
            std::string status = "{ \"running\": " + std::string(crawlerEnabled.load() ? "true" : "false") + ", ";
            status += "\"queue_size\": " + std::to_string(urlQueueManager ? urlQueueManager->getQueueSize() : 0) + ", ";
            status += "\"processed_urls\": " + std::to_string(urlQueueManager ? urlQueueManager->getProcessedCount() : 0) + ", ";
            status += "\"books_found\": " + std::to_string(urlQueueManager ? urlQueueManager->getBookCount() : 0) + ", ";
            status += "\"items_found\": " + std::to_string(urlQueueManager ? urlQueueManager->getItemCount() : 0) + ", ";
            status += "\"workers\": " + std::to_string(workerRegistry.getActiveWorkerCount()) + ", ";
            status += "\"seed_url\": \"" + (urlQueueManager ? urlQueueManager->getSeedUrl() : "") + "\", ";
            status += "\"item_type\": \"" + (urlQueueManager ? urlQueueManager->getItemTypeString() : "UNKNOWN") + "\", ";
            status += "\"server_status\": \"running\" }";
            response = status;
            contentType = "application/json";
        }
        else if (request.find("POST /api/seed") != std::string::npos) {
            // Extract URL from request body
            size_t bodyStart = request.find("\r\n\r\n");
            if (bodyStart != std::string::npos) {
                std::string body = request.substr(bodyStart + 4);
                std::string url = body;
                
                // Set the seed URL
                if (urlQueueManager) {
                    urlQueueManager->setSeedUrl(url);
                    response = "{ \"status\": \"success\", \"message\": \"Seed URL set successfully\" }";
                } else {
                    response = "{ \"error\": \"URL queue manager not initialized\" }";
                    statusCode = 500;
                }
            } else {
                response = "{ \"error\": \"No URL provided in request body\" }";
                statusCode = 400;
            }
            contentType = "application/json";
        }
        else if (request.find("POST /api/start") != std::string::npos) {
            // Start the crawler
            crawlerEnabled.store(true);
            if (urlQueueManager) {
                // Add the seed URL to the queue if it's not already there
                std::string seedUrl = urlQueueManager->getSeedUrl();
                if (!seedUrl.empty()) {
                    urlQueueManager->addSeedUrl(seedUrl);
                }
            }
            response = "{ \"status\": \"success\", \"message\": \"Crawler started successfully\" }";
            contentType = "application/json";
        }
        else if (request.find("GET /") != std::string::npos || request.find("GET /index.html") != std::string::npos) {
            // Serve the frontend HTML
            response = getHtmlFrontend();
            contentType = "text/html";
            if (response.find("Error: Frontend file not found") != std::string::npos) {
                statusCode = 500;
            }
        } 
        else {
            // Unknown endpoint
            response = "{ \"error\": \"Unknown endpoint\" }";
            contentType = "application/json";
            statusCode = 404;
        }
        
        // Send HTTP response
        std::string httpResponse = "HTTP/1.1 " + std::to_string(statusCode) + " " + 
                                 (statusCode == 200 ? "OK" : (statusCode == 404 ? "Not Found" : "Internal Server Error")) + "\r\n";
        httpResponse += "Content-Type: " + contentType + "\r\n";
        httpResponse += "Content-Length: " + std::to_string(response.length()) + "\r\n";
        httpResponse += "Access-Control-Allow-Origin: *\r\n";  // CORS header
        httpResponse += "Connection: close\r\n\r\n";
        httpResponse += response;
        
        send(clientSocket, httpResponse.c_str(), httpResponse.length(), 0);
        
        // Close the connection
        CLOSE_SOCKET(clientSocket);
    }
    
    std::string getHtmlFrontend() {
        std::ifstream in("frontend.html", std::ios::in | std::ios::binary);
        if (!in) {
            logMessage("Error: cannot open frontend.html");
            return "<!DOCTYPE html><html><body><h1>Error: Frontend file not found</h1><p>The frontend.html file could not be loaded.</p></body></html>";
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }
};

// Global API handler
ApiHandler* apiHandler = nullptr;

int main() {
    #ifdef _WIN32
    // Initialize Winsock on Windows
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        return 1;
    }
    #endif
    
    // Set up signal handlers
    #ifdef _WIN32
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    #else
    struct sigaction sa;
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    #endif
    
    // Start the shutdown monitor thread
    std::thread shutdownThread(checkShutdown);
    shutdownThread.detach();
    
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
    
    // Set up server address
    struct sockaddr_in serverAddr;
    ZeroMemory(&serverAddr, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(SERVER_PORT);
    
    // Bind socket
    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        #ifdef _WIN32
        std::cerr << "Error binding socket: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        #else
        std::cerr << "Error binding socket" << std::endl;
        close(serverSocket);
        #endif
        return 1;
    }
    
    // Listen for connections
    if (listen(serverSocket, BACKLOG) == SOCKET_ERROR) {
        #ifdef _WIN32
        std::cerr << "Error listening on socket: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        #else
        std::cerr << "Error listening on socket" << std::endl;
        close(serverSocket);
        #endif
        return 1;
    }
    
    // Initialize URL queue manager
    initUrlQueueManager();
    
    // Initialize status display
    logMessage("Starting server on port " + std::to_string(SERVER_PORT));
    
    // Start the status display thread
    std::thread statusThread(displayStatus);
    statusThread.detach();
    
    // Start the API server
    apiHandler = new ApiHandler();
    apiHandler->start();
    logMessage("API server started on port " + std::to_string(WEB_PORT));
    logMessage("Web interface available at http://localhost:" + std::to_string(WEB_PORT));
    
    // Main accept loop
    while (!serverShutdown.load()) {
        // Set up select() for non-blocking accept
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(serverSocket, &readfds);
        
        // Set timeout to 1 second
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        // Wait for activity on the socket
        int activity = select(serverSocket + 1, &readfds, NULL, NULL, &timeout);
        
        if (activity == SOCKET_ERROR) {
            logMessage("Select error");
            break;
        }
        
        // Check if we have a connection
        if (activity > 0 && FD_ISSET(serverSocket, &readfds)) {
            struct sockaddr_in clientAddr;
            socklen_t clientAddrSize = sizeof(clientAddr);
            
            SOCKET clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrSize);
            if (clientSocket == INVALID_SOCKET) {
                logMessage("Error accepting connection");
                continue;
            }
            
            // Get client IP and port
            char clientIP[INET_ADDRSTRLEN];
            #ifdef _WIN32
            inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);
            #else
            inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);
            #endif
            int clientPort = ntohs(clientAddr.sin_port);
            
            logMessage("Accepted connection from " + std::string(clientIP) + ":" + std::to_string(clientPort));
            
            // Create a new thread to handle this client
            std::thread clientThread(handleClient, clientSocket, std::string(clientIP), clientPort);
            clientThread.detach();
        }
    }
    
    // Clean up
    CLOSE_SOCKET(serverSocket);
    
    // Clean up API server
    if (apiHandler != nullptr) {
        delete apiHandler;
        apiHandler = nullptr;
    }
    
    // Save books
    if (urlQueueManager != nullptr) {
        urlQueueManager->saveCollectedBooks("books.csv");
    }
    
    // Cleanup Winsock on Windows
    #ifdef _WIN32
    WSACleanup();
    #endif
    
    logMessage("Server shutdown complete.");
    return 0;
} 