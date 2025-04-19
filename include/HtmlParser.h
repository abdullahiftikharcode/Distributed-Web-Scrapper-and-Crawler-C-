#ifndef HTML_PARSER_H
#define HTML_PARSER_H

#include <string>
#include <vector>
#include <set>
#include "Book.h"

// Extract text between two delimiters
std::string extract_between(const std::string& text, const std::string& start_delim, const std::string& end_delim);

// Parse books from HTML content
std::vector<Book> parse_books(const std::string& html, const std::string& base_url);

// Parse a single book page and extract book information
Book parse_book_page(const std::string& html, const std::string& hostname, const std::string& url);

// Find the next page link in the HTML
std::string find_next_link(const std::string& html);

// Extract all hyperlinks from the HTML
std::set<std::string> extract_all_links(const std::string& html, const std::string& base_url);

// Normalize URL (convert relative to absolute)
std::string normalize_url(const std::string& url, const std::string& base_url);

// Canonicalize URL for deduplication (handle www, trailing slashes, etc.)
std::string canonicalize_url(const std::string& url);

// Check if URL is a book detail page
bool is_book_page(const std::string& url);

// Check if URL is a category page
bool is_category_page(const std::string& url);

// Check if URL should be ignored (e.g., login, admin pages)
bool should_ignore_url(const std::string& url);

#endif // HTML_PARSER_H 