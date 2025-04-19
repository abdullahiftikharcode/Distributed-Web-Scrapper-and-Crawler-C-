#ifndef BOOK_H
#define BOOK_H

#include <string>

struct Book {
    std::string title;
    std::string price;
    std::string rating; // e.g., "Three", "Four", etc.
    std::string url;    // The URL where this book was found
    
    // Equality comparison based on URL
    bool operator==(const Book& other) const {
        return url == other.url;
    }
    
    // Less than comparison for use in sets
    bool operator<(const Book& other) const {
        return url < other.url;
    }
};

#endif // BOOK_H 