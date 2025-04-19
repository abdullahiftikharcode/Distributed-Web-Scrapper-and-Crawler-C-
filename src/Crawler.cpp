#include "../include/Crawler.h"
#include "../include/HttpClient.h"
#include "../include/HtmlParser.h"
#include <iostream>
#include <chrono>
#include <conio.h>  // For _kbhit()

std::vector<Book> crawl_website(const std::string& hostname, const std::string& start_path, int max_pages) {
    std::vector<Book> all_books;
    std::string current_path = start_path;
    int pages_crawled = 0;
    bool crawl_all = (max_pages <= 0);  // If max_pages is 0 or negative, crawl all available pages
    
    // Set to track book URLs to prevent duplicates
    std::set<std::string> book_urls;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::cout << "Crawling started. Press any key to stop..." << std::endl;
    
    std::string base_url = "http://" + hostname;
    
    while (!current_path.empty() && (crawl_all || pages_crawled < max_pages)) {
        // Check if a key was pressed to stop crawling
        if (_kbhit()) {
            std::cout << "\nKey pressed. Stopping crawler..." << std::endl;
            break;
        }
        
        std::cout << "Crawling page " << (pages_crawled + 1) << ": " << current_path << std::endl;
        
        // Time the HTTP request
        auto http_start = std::chrono::high_resolution_clock::now();
        std::string response = http_get(hostname, current_path);
        auto http_end = std::chrono::high_resolution_clock::now();
        
        if (response.empty()) {
            std::cerr << "Failed to get response for " << current_path << std::endl;
            break;
        }
        
        // Extract the HTML body from the HTTP response
        std::string html = extract_body(response);
        
        // Time the parsing
        auto parse_start = std::chrono::high_resolution_clock::now();
        std::vector<Book> page_books = parse_books(html, base_url + current_path);
        auto parse_end = std::chrono::high_resolution_clock::now();
        
        // Track new and duplicate books
        int new_books = 0;
        int duplicate_books = 0;
        
        // Add new books to our collection, avoiding duplicates
        for (const auto& book : page_books) {
            // Use the canonicalized URL for deduplication
            std::string canonical_url = canonicalize_url(book.url);
            
            if (book_urls.find(canonical_url) == book_urls.end()) {
                all_books.push_back(book);
                book_urls.insert(canonical_url);
                new_books++;
            } else {
                duplicate_books++;
            }
        }
        
        std::cout << "Found " << page_books.size() << " books on this page" << std::endl;
        std::cout << "Added " << new_books << " new books" << std::endl;
        std::cout << "Skipped " << duplicate_books << " duplicate books" << std::endl;
        
        // Find the next page link
        std::string next_link = find_next_link(html);
        
        // Print timing information
        std::chrono::duration<double, std::milli> http_duration = http_end - http_start;
        std::chrono::duration<double, std::milli> parse_duration = parse_end - parse_start;
        
        std::cout << "HTTP request took " << http_duration.count() << " ms" << std::endl;
        std::cout << "Parsing took " << parse_duration.count() << " ms" << std::endl;
        std::cout << "Total books found so far: " << all_books.size() << std::endl;
        std::cout << "----------------------------------------------------" << std::endl;
        
        // Update for next iteration
        current_path = next_link;
        pages_crawled++;
        
        // If there's no next link, we've reached the end
        if (current_path.empty()) {
            std::cout << "No more pages to crawl." << std::endl;
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> total_duration = end_time - start_time;
    
    std::cout << "\nCrawling completed:" << std::endl;
    std::cout << "Total pages crawled: " << pages_crawled << std::endl;
    std::cout << "Total unique books found: " << all_books.size() << std::endl;
    std::cout << "Total time: " << total_duration.count() << " seconds" << std::endl;
    
    return all_books;
}

// Queue-based crawling
std::vector<Book> crawl_website_queue(const std::string& hostname, const std::string& start_path, int max_pages) {
    std::vector<Book> all_books;
    int pages_crawled = 0;
    bool crawl_all = (max_pages <= 0);  // If max_pages is 0 or negative, crawl all available pages
    
    // Queue of URLs to be processed
    std::queue<std::string> pending_urls;
    
    // Set of URLs that have been processed or are in the queue (stored in canonicalized form)
    std::set<std::string> processed_urls;
    
    // Set of URLs that are currently being processed
    std::set<std::string> processing_urls;
    
    // Set of book URLs to prevent duplicates (stored in canonicalized form)
    std::set<std::string> book_urls;
    
    // Start with the initial URL
    std::string base_url = "http://" + hostname;
    std::string full_start_url = base_url + start_path;
    
    // Add starting URL to queue
    pending_urls.push(start_path);
    
    // Also add its canonicalized form to processed set
    std::string canonical_start = canonicalize_url(full_start_url);
    processed_urls.insert(canonical_start);
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::cout << "Queue-based crawling started. Press any key to stop..." << std::endl;
    
    // Track statistics
    int duplicate_count = 0;
    int ignored_count = 0;
    int duplicate_book_count = 0;
    
    while (!pending_urls.empty() && (crawl_all || pages_crawled < max_pages)) {
        // Check if a key was pressed to stop crawling
        if (_kbhit()) {
            std::cout << "\nKey pressed. Stopping crawler..." << std::endl;
            break;
        }
        
        // Get the next URL from the queue
        std::string current_path = pending_urls.front();
        pending_urls.pop();
        
        // Move to processing set
        processing_urls.insert(current_path);
        
        std::cout << "Crawling page " << (pages_crawled + 1) << ": " << current_path << std::endl;
        
        // Time the HTTP request
        auto http_start = std::chrono::high_resolution_clock::now();
        std::string response = http_get(hostname, current_path);
        auto http_end = std::chrono::high_resolution_clock::now();
        
        if (response.empty()) {
            std::cerr << "Failed to get response for " << current_path << std::endl;
            // Remove from processing, but keep in processed to avoid reprocessing
            processing_urls.erase(current_path);
            continue;
        }
        
        // Extract the HTML body from the HTTP response
        std::string html = extract_body(response);
        
        // Time the parsing
        auto parse_start = std::chrono::high_resolution_clock::now();
        
        // Parse books from this page if it's a book listing page
        int new_books = 0;
        int page_duplicate_books = 0;
        
        if (is_category_page(current_path) || current_path.find("index.html") != std::string::npos || 
            current_path.find("page-") != std::string::npos) {
            
            std::vector<Book> page_books = parse_books(html, base_url + current_path);
            
            std::cout << "Found " << page_books.size() << " books on this page" << std::endl;
            
            // Add books to our collection, avoiding duplicates
            for (const auto& book : page_books) {
                // Use the canonicalized URL for book deduplication
                std::string canonical_book_url = canonicalize_url(book.url);
                
                if (book_urls.find(canonical_book_url) == book_urls.end()) {
                    all_books.push_back(book);
                    book_urls.insert(canonical_book_url);
                    new_books++;
                } else {
                    page_duplicate_books++;
                    duplicate_book_count++;
                }
            }
        }
        
        // Extract all links from this page
        std::set<std::string> links = extract_all_links(html, base_url + current_path);
        
        // Update the queue with new links
        int new_links = 0;
        int page_duplicates = 0;
        int page_ignored = 0;
        
        for (const auto& link : links) {
            // First check if we should ignore this URL
            if (should_ignore_url(link)) {
                page_ignored++;
                ignored_count++;
                continue;
            }
            
            // Convert to relative path for consistency
            std::string relative_path = link;
            if (link.find(base_url) == 0) {
                relative_path = link.substr(base_url.length());
            }
            
            // Get canonical form for deduplication checking
            std::string canonical_url = canonicalize_url(link);
            
            // Check if we've already processed or queued this URL
            if (processed_urls.find(canonical_url) == processed_urls.end()) {
                // Add to pending queue
                pending_urls.push(relative_path);
                
                // Mark canonical form as processed to avoid duplicates
                processed_urls.insert(canonical_url);
                new_links++;
            } else {
                // Track duplicates
                page_duplicates++;
                duplicate_count++;
            }
        }
        
        auto parse_end = std::chrono::high_resolution_clock::now();
        
        // Print timing information
        std::chrono::duration<double, std::milli> http_duration = http_end - http_start;
        std::chrono::duration<double, std::milli> parse_duration = parse_end - parse_start;
        
        std::cout << "HTTP request took " << http_duration.count() << " ms" << std::endl;
        std::cout << "Parsing took " << parse_duration.count() << " ms" << std::endl;
        std::cout << "Found " << links.size() << " total links on the page" << std::endl;
        std::cout << "Added " << new_links << " new links to queue" << std::endl;
        std::cout << "Skipped " << page_duplicates << " duplicate URLs" << std::endl;
        std::cout << "Ignored " << page_ignored << " irrelevant URLs" << std::endl;
        
        if (new_books > 0 || page_duplicate_books > 0) {
            std::cout << "Added " << new_books << " new books" << std::endl;
            std::cout << "Skipped " << page_duplicate_books << " duplicate books" << std::endl;
        }
        
        std::cout << "Pending URLs: " << pending_urls.size() << std::endl;
        std::cout << "Processed URLs: " << processed_urls.size() << std::endl;
        std::cout << "Total unique books found so far: " << all_books.size() << std::endl;
        std::cout << "----------------------------------------------------" << std::endl;
        
        // Remove from processing set
        processing_urls.erase(current_path);
        
        // Increment page counter
        pages_crawled++;
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> total_duration = end_time - start_time;
    
    std::cout << "\nCrawling completed:" << std::endl;
    std::cout << "Total pages crawled: " << pages_crawled << std::endl;
    std::cout << "Total unique URLs found: " << processed_urls.size() << std::endl;
    std::cout << "Total duplicate URLs skipped: " << duplicate_count << std::endl;
    std::cout << "Total irrelevant URLs ignored: " << ignored_count << std::endl;
    std::cout << "Total unique books found: " << all_books.size() << std::endl;
    std::cout << "Total duplicate books skipped: " << duplicate_book_count << std::endl;
    std::cout << "Queue size at completion: " << pending_urls.size() << std::endl;
    std::cout << "Total time: " << total_duration.count() << " seconds" << std::endl;
    
    return all_books;
} 