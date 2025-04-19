#include "../include/HtmlParser.h"
#include <algorithm>
#include <iostream>

// Extract text between two delimiters
std::string extract_between(const std::string& text, const std::string& start_delim, const std::string& end_delim) {
    size_t start_pos = text.find(start_delim);
    if (start_pos == std::string::npos) {
        return "";
    }
    
    start_pos += start_delim.length();
    size_t end_pos = text.find(end_delim, start_pos);
    if (end_pos == std::string::npos) {
        return "";
    }
    
    return text.substr(start_pos, end_pos - start_pos);
}

// Parse the rating value from the class attribute
std::string parse_rating(const std::string& rating_class) {
    if (rating_class.find("One") != std::string::npos) return "One";
    if (rating_class.find("Two") != std::string::npos) return "Two";
    if (rating_class.find("Three") != std::string::npos) return "Three";
    if (rating_class.find("Four") != std::string::npos) return "Four";
    if (rating_class.find("Five") != std::string::npos) return "Five";
    return "Unknown";
}

// Parse books from HTML content
std::vector<Book> parse_books(const std::string& html, const std::string& base_url) {
    std::vector<Book> books;
    
    // Each book is contained within an "article" tag with class "product_pod"
    const std::string book_start = "<article class=\"product_pod\">";
    const std::string book_end = "</article>";
    
    size_t pos = 0;
    while ((pos = html.find(book_start, pos)) != std::string::npos) {
        size_t book_end_pos = html.find(book_end, pos);
        if (book_end_pos == std::string::npos) {
            break;
        }
        
        // Extract the HTML for this book
        std::string book_html = html.substr(pos, book_end_pos - pos + book_end.length());
        
        Book book;
        
        // Extract title - it's in the h3 tag and inside an a tag with title attribute
        std::string title_tag = extract_between(book_html, "<h3>", "</h3>");
        std::string title_attr = extract_between(title_tag, "title=\"", "\"");
        book.title = title_attr;
        
        // If title attribute extraction failed, try getting text content
        if (book.title.empty()) {
            book.title = extract_between(title_tag, "\">", "</a>");
        }
        
        // Extract the book detail URL - it's in the a tag as href attribute
        std::string book_relative_url = extract_between(title_tag, "href=\"", "\"");
        book.url = normalize_url(book_relative_url, base_url);
        
        // Extract price - it's in a p tag with class "price_color"
        std::string price = extract_between(book_html, "<p class=\"price_color\">", "</p>");
        book.price = price;
        
        // Extract rating - it's in a p tag with class "star-rating" followed by the rating level
        std::string rating_class = extract_between(book_html, "<p class=\"star-rating ", "\"");
        book.rating = parse_rating(rating_class);
        
        // Only add book if it has a valid URL (to avoid duplicates)
        if (!book.url.empty()) {
            books.push_back(book);
        }
        
        // Move past this book to find the next one
        pos = book_end_pos + book_end.length();
    }
    
    return books;
}

// Find the next page link in the HTML
std::string find_next_link(const std::string& html) {
    // Look for the "next" link in the pagination section
    const std::string next_link_start = "<li class=\"next\">";
    const std::string next_link_end = "</li>";
    
    std::string next_li = extract_between(html, next_link_start, next_link_end);
    if (next_li.empty()) {
        return ""; // No next page
    }
    
    // Extract the href attribute from the a tag
    std::string href = extract_between(next_li, "href=\"", "\"");
    return href;
}

// Normalize URL (convert relative to absolute)
std::string normalize_url(const std::string& url, const std::string& base_url) {
    // If URL starts with http:// or https://, it's already absolute
    if (url.find("http://") == 0 || url.find("https://") == 0) {
        return url;
    }
    
    // Empty URLs aren't valid
    if (url.empty()) {
        return "";
    }
    
    // If URL starts with '//', add http:
    if (url.substr(0, 2) == "//") {
        return "http:" + url;
    }
    
    // Extract domain from base_url to prevent domain concatenation issues
    std::string domain = "";
    size_t domain_start = base_url.find("://");
    if (domain_start != std::string::npos) {
        domain_start += 3; // Skip "://"
        size_t domain_end = base_url.find('/', domain_start);
        if (domain_end != std::string::npos) {
            domain = base_url.substr(0, domain_end); // Include protocol
        } else {
            domain = base_url; // The base_url is just the domain
        }
    } else {
        // If no protocol in base_url, assume http://
        domain = "http://" + base_url.substr(0, base_url.find('/'));
    }
    
    // If URL starts with '/', it's relative to domain root
    if (url[0] == '/') {
        return domain + url;
    }
    
    // Otherwise, it's relative to current path
    size_t last_slash = base_url.find_last_of('/');
    if (last_slash != std::string::npos && last_slash > 8) { // 8 to skip http(s)://
        return base_url.substr(0, last_slash + 1) + url;
    }
    
    return domain + "/" + url;
}

// Extract all hyperlinks from the HTML
std::set<std::string> extract_all_links(const std::string& html, const std::string& base_url) {
    std::set<std::string> links;
    
    // Look for all a tags with href attributes
    const std::string a_href = "href=\"";
    
    size_t pos = 0;
    while ((pos = html.find(a_href, pos)) != std::string::npos) {
        pos += a_href.length();
        size_t end_pos = html.find("\"", pos);
        if (end_pos == std::string::npos) {
            break;
        }
        
        std::string href = html.substr(pos, end_pos - pos);
        
        // Skip URLs that are obviously not content (static resources, etc.)
        if (href.find(".css") != std::string::npos ||
            href.find(".js") != std::string::npos ||
            href.find(".ico") != std::string::npos ||
            href.find(".jpg") != std::string::npos ||
            href.find(".png") != std::string::npos ||
            href.find("/static/") != std::string::npos) {
            pos = end_pos + 1;
            continue;
        }
        
        // Normalize the URL
        std::string full_url = normalize_url(href, base_url);
        
        // Skip malformed URLs
        if (full_url.find("http://books.toscrape.comhttp") != std::string::npos ||
            full_url.find("http://books.toscrape.comhttps") != std::string::npos ||
            full_url.find("mhttp") != std::string::npos ||
            full_url.find("mhttps") != std::string::npos) {
            pos = end_pos + 1;
            continue;
        }
        
        // Only add if it's from the same domain (books.toscrape.com)
        if (!full_url.empty() && full_url.find("books.toscrape.com") != std::string::npos) {
            links.insert(full_url);
        }
        
        pos = end_pos + 1;
    }
    
    return links;
}

// Check if URL is a book detail page
bool is_book_page(const std::string& url) {
    // Book detail pages contain "/catalogue/" and don't end with .html but have no trailing slash
    return url.find("/catalogue/") != std::string::npos && 
           url.rfind(".html") == std::string::npos &&
           url.back() != '/';
}

// Check if URL is a category page
bool is_category_page(const std::string& url) {
    // Category pages are in /category/ directory and typically end with index.html or page-X.html
    return url.find("/category/") != std::string::npos ||
           url.find("index.html") != std::string::npos ||
           url.find("page-") != std::string::npos;
}

// Canonicalize URL for deduplication
std::string canonicalize_url(const std::string& url) {
    std::string result = url;
    
    // Convert to lowercase for case-insensitive comparison
    std::transform(result.begin(), result.end(), result.begin(), 
                  [](unsigned char c){ return std::tolower(c); });
    
    // Remove protocol part for matching
    size_t protocol_pos = result.find("://");
    if (protocol_pos != std::string::npos) {
        result = result.substr(protocol_pos + 3);
    }
    
    // Remove 'www.' prefix if present
    if (result.substr(0, 4) == "www.") {
        result = result.substr(4);
    }
    
    // Handle trailing slashes consistently
    if (!result.empty() && result.back() == '/') {
        result.pop_back();
    }
    
    // Remove anchor part (anything after #)
    size_t anchor_pos = result.find('#');
    if (anchor_pos != std::string::npos) {
        result = result.substr(0, anchor_pos);
    }
    
    // Remove query parameters for page content matching
    // Only do this for pages that aren't search or filtered results
    size_t query_pos = result.find('?');
    if (query_pos != std::string::npos && 
        result.find("search") == std::string::npos && 
        result.find("filter") == std::string::npos) {
        result = result.substr(0, query_pos);
    }
    
    return result;
}

// Check if URL should be ignored
bool should_ignore_url(const std::string& url) {
    // Check for irrelevant sections
    if (url.find("/accounts/") != std::string::npos ||
        url.find("/login") != std::string::npos ||
        url.find("/logout") != std::string::npos ||
        url.find("/admin") != std::string::npos ||
        url.find("/static/") != std::string::npos ||
        url.find(".jpg") != std::string::npos ||
        url.find(".png") != std::string::npos ||
        url.find(".css") != std::string::npos ||
        url.find(".js") != std::string::npos) {
        return true;
    }
    
    // Non-books.toscrape.com URLs
    if (url.find("books.toscrape.com") == std::string::npos) {
        return true;
    }
    
    return false;
}

// Add this at the end of the file
Book parse_book_page(const std::string& html, const std::string& hostname, const std::string& url) {
    Book book;
    
    // Initialize the book with empty fields
    book.title = "";
    book.price = "";
    book.rating = "";
    book.url = url;
    
    // Extract the title
    std::string title_start = "<h1>";
    std::string title_end = "</h1>";
    book.title = extract_between(html, title_start, title_end);
    
    // Extract the price
    std::string price_start = "<p class=\"price_color\">";
    std::string price_end = "</p>";
    book.price = extract_between(html, price_start, price_end);
    
    // Extract the rating - find the star-rating class
    size_t ratingPos = html.find("<p class=\"star-rating");
    if (ratingPos != std::string::npos) {
        // Find the closing quote after the class name
        size_t classStart = ratingPos + 18; // Length of "<p class=\"star-rating"
        size_t classEnd = html.find("\"", classStart);
        
        if (classEnd != std::string::npos) {
            // Extract the class which contains the rating
            std::string ratingClass = html.substr(classStart, classEnd - classStart);
            
            // Convert the rating class to a rating value
            if (ratingClass.find("One") != std::string::npos) {
                book.rating = "One";
            } else if (ratingClass.find("Two") != std::string::npos) {
                book.rating = "Two";
            } else if (ratingClass.find("Three") != std::string::npos) {
                book.rating = "Three";
            } else if (ratingClass.find("Four") != std::string::npos) {
                book.rating = "Four";
            } else if (ratingClass.find("Five") != std::string::npos) {
                book.rating = "Five";
            } else {
                book.rating = "Unknown";
            }
        }
    }
    
    return book;
} 