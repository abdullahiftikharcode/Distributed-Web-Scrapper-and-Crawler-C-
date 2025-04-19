#include "../include/Book.h"
#include "../include/Crawler.h"
#include "../include/HtmlParser.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <set>

void print_book(const Book& book) {
    std::cout << "Title: " << book.title << std::endl;
    std::cout << "Price: " << book.price << std::endl;
    std::cout << "Rating: " << book.rating << std::endl;
    std::cout << "URL: " << book.url << std::endl;
    std::cout << "-------------------------" << std::endl;
}

void save_to_csv(const std::vector<Book>& books, const std::string& filename) {
    std::ofstream outfile(filename);
    if (!outfile) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return;
    }
    
    // Write CSV header
    outfile << "Title,Price,Rating,URL\n";
    
    // Write book data
    for (const auto& book : books) {
        // Escape double quotes in the title by doubling them
        std::string escaped_title = book.title;
        size_t pos = 0;
        while ((pos = escaped_title.find("\"", pos)) != std::string::npos) {
            escaped_title.replace(pos, 1, "\"\"");
            pos += 2;
        }
        
        outfile << "\"" << escaped_title << "\","
                << "\"" << book.price << "\","
                << "\"" << book.rating << "\","
                << "\"" << book.url << "\"\n";
    }
    
    outfile.close();
    std::cout << "Data saved to " << filename << std::endl;
}

void display_help() {
    std::cout << "Web Scraper Usage:" << std::endl;
    std::cout << "  webscraper [options] [max_pages]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --help        Show this help message" << std::endl;
    std::cout << "  -s, --sequential  Use sequential crawling (default: queue-based)" << std::endl;
    std::cout << std::endl;
    std::cout << "Arguments:" << std::endl;
    std::cout << "  max_pages         Maximum number of pages to crawl (optional)" << std::endl;
    std::cout << "                    Use 0 or a negative number to crawl all available pages" << std::endl;
    std::cout << "                    Default: 0 (crawl all pages)" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  webscraper              # Crawl all available pages using queue-based approach" << std::endl;
    std::cout << "  webscraper 5            # Crawl maximum 5 pages using queue-based approach" << std::endl;
    std::cout << "  webscraper -s           # Crawl all available pages sequentially" << std::endl;
    std::cout << "  webscraper -s 5         # Crawl maximum 5 pages sequentially" << std::endl;
}

// Function to deduplicate books based on their URLs
std::vector<Book> deduplicate_books(const std::vector<Book>& books) {
    std::vector<Book> unique_books;
    std::set<std::string> seen_urls;
    
    int duplicates = 0;
    
    for (const auto& book : books) {
        std::string canonical_url = canonicalize_url(book.url);
        
        if (seen_urls.find(canonical_url) == seen_urls.end()) {
            unique_books.push_back(book);
            seen_urls.insert(canonical_url);
        } else {
            duplicates++;
        }
    }
    
    if (duplicates > 0) {
        std::cout << "Removed " << duplicates << " duplicate books during final deduplication" << std::endl;
    }
    
    return unique_books;
}

// The main scraper functionality
int run_cli_scraper(int argc, char* argv[]) {
    const std::string hostname = "books.toscrape.com";
    const std::string start_path = "/catalogue/page-1.html";
    int max_pages = 0; // Default to crawl all pages
    bool use_queue = true; // Default to queue-based crawling
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            display_help();
            return 0;
        } else if (arg == "-s" || arg == "--sequential") {
            use_queue = false; // Override to use sequential crawling
        } else {
            // Assume it's the max_pages value
            try {
                max_pages = std::stoi(arg);
            } catch (const std::exception& e) {
                std::cerr << "Invalid argument: " << arg << std::endl;
                std::cerr << "Using default value of " << max_pages << " (crawl all pages)" << std::endl;
            }
        }
    }
    
    std::cout << "Web Scraper for " << hostname << std::endl;
    std::cout << "Starting from: " << start_path << std::endl;
    std::cout << "Crawling method: " << (use_queue ? "Queue-based" : "Sequential") << std::endl;
    
    if (max_pages > 0) {
        std::cout << "Maximum pages to crawl: " << max_pages << std::endl;
    } else {
        std::cout << "Will crawl all available pages (press any key to stop)" << std::endl;
    }
    std::cout << "-------------------------" << std::endl;
    
    // Crawl the website
    std::vector<Book> books;
    if (use_queue) {
        books = crawl_website_queue(hostname, start_path, max_pages);
    } else {
        books = crawl_website(hostname, start_path, max_pages);
    }
    
    if (books.empty()) {
        std::cout << "No books were found." << std::endl;
        return 1;
    }
    
    // Final deduplication pass to ensure no duplicates
    books = deduplicate_books(books);
    
    // Print sample of books (first 5)
    std::cout << "\nBook Sample (first 5 or fewer):" << std::endl;
    int sample_size = std::min(5, static_cast<int>(books.size()));
    for (int i = 0; i < sample_size; i++) {
        print_book(books[i]);
    }
    
    // Save results to CSV
    save_to_csv(books, "books.csv");
    
    return 0;
}

// Main entry point
int main(int argc, char* argv[]) {
    return run_cli_scraper(argc, argv);
} 