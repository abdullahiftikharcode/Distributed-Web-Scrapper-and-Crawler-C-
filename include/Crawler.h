#ifndef CRAWLER_H
#define CRAWLER_H

#include <vector>
#include <string>
#include <queue>
#include <set>
#include "Book.h"

// Crawl the website using a page limit approach
std::vector<Book> crawl_website(const std::string& hostname, const std::string& start_path, int max_pages);

// Crawl the website using a queue-based approach
std::vector<Book> crawl_website_queue(const std::string& hostname, const std::string& start_path, int max_pages);

#endif // CRAWLER_H 