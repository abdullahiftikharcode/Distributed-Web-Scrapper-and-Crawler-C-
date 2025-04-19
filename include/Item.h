#ifndef ITEM_H
#define ITEM_H

#include <string>
#include <map>
#include <vector>
#include <iostream>

// Define item types
enum class ItemType {
    BOOK,
    JOB,
    PRODUCT,
    ARTICLE,
    GENERIC
};

// Generic item structure that can represent different types of crawled content
class Item {
public:
    std::string id;                            // Unique identifier (usually URL or specific ID)
    ItemType type;                             // Type of the item
    std::string url;                           // Source URL
    std::string title;                         // Title/Name
    std::map<std::string, std::string> fields; // Additional fields (vary by type)
    std::string category;                      // Item category
    double price = 0.0;                        // Price (if applicable)
    int rating = 0;                            // Rating (0-5)
    std::string description;                   // Description text
    std::string imageUrl;                      // URL to image
    std::string date;                          // Publication/posting date
    
    // Constructor for a basic item
    Item() : type(ItemType::GENERIC) {}
    
    // Constructor with type
    Item(ItemType itemType) : type(itemType) {}
    
    // Convert from a Book to an Item
    static Item fromBook(const struct Book& book) {
        Item item(ItemType::BOOK);
        item.title = book.title;
        item.url = book.url;
        item.id = book.url;
        
        // Extract price as double if possible
        if (!book.price.empty()) {
            // Try to parse price - handle common currency symbols (£, $, €)
            std::string priceStr = book.price;
            
            // Debug logging
            std::cout << "Original price string: '" << priceStr << "'" << std::endl;
            
            std::string cleanPrice;
            bool foundDigit = false;
            
            // Extract only digits and decimal point
            for (char c : priceStr) {
                // Convert to ASCII to handle UTF-8 encoding issues
                unsigned char uc = static_cast<unsigned char>(c);
                
                // If the first character is a currency symbol, skip it
                if (!foundDigit && (c == L'£' || c == L'$' || c == L'€' || uc > 127)) {
                    continue;
                }
                
                if (std::isdigit(c)) {
                    cleanPrice += c;
                    foundDigit = true;
                } else if ((c == '.' || c == ',') && foundDigit) {
                    // Replace comma with decimal point if needed
                    cleanPrice += '.';
                }
            }
            
            std::cout << "Cleaned price string: '" << cleanPrice << "'" << std::endl;
            
            try {
                if (!cleanPrice.empty()) {
                    item.price = std::stod(cleanPrice);
                    std::cout << "Converted price: " << item.price << std::endl;
                } else {
                    item.price = 0.0;
                }
            } catch (...) {
                std::cout << "Price conversion exception" << std::endl;
                item.price = 0.0;
            }
        }
        
        // Convert rating string to number
        if (book.rating == "One") item.rating = 1;
        else if (book.rating == "Two") item.rating = 2;
        else if (book.rating == "Three") item.rating = 3;
        else if (book.rating == "Four") item.rating = 4;
        else if (book.rating == "Five") item.rating = 5;
        
        // Store original values in fields
        item.fields["price_original"] = book.price;
        item.fields["rating_original"] = book.rating;
        
        return item;
    }
    
    // Create a job listing
    static Item createJobListing(const std::string& title, const std::string& url, 
                                const std::string& company, const std::string& location,
                                const std::string& salary, const std::string& description) {
        Item item(ItemType::JOB);
        item.title = title;
        item.url = url;
        item.id = url;
        item.description = description;
        
        item.fields["company"] = company;
        item.fields["location"] = location;
        item.fields["salary"] = salary;
        
        // Try to parse salary if possible
        if (!salary.empty()) {
            std::string salaryStr = salary;
            // Remove non-numeric chars except decimal point
            salaryStr.erase(
                std::remove_if(salaryStr.begin(), salaryStr.end(), 
                            [](char c) { return !std::isdigit(c) && c != '.'; }),
                salaryStr.end());
            try {
                item.price = std::stod(salaryStr);
            } catch (...) {
                item.price = 0.0;
            }
        }
        
        return item;
    }
    
    // Create a product
    static Item createProduct(const std::string& title, const std::string& url,
                             double price, int rating, const std::string& category,
                             const std::string& imageUrl, const std::string& description) {
        Item item(ItemType::PRODUCT);
        item.title = title;
        item.url = url;
        item.id = url;
        item.price = price;
        item.rating = rating;
        item.category = category;
        item.imageUrl = imageUrl;
        item.description = description;
        
        return item;
    }
    
    // Equality comparison based on ID
    bool operator==(const Item& other) const {
        return id == other.id;
    }
    
    // Less than comparison for use in sets
    bool operator<(const Item& other) const {
        return id < other.id;
    }
    
    // Convert to string representation
    std::string toString() const {
        std::string result = "Item[" + title + ", type=" + typeToString() + ", url=" + url;
        if (price > 0) {
            result += ", price=" + std::to_string(price);
        }
        if (rating > 0) {
            result += ", rating=" + std::to_string(rating);
        }
        result += "]";
        return result;
    }
    
    // Get type as string
    std::string typeToString() const {
        switch (type) {
            case ItemType::BOOK: return "Book";
            case ItemType::JOB: return "Job";
            case ItemType::PRODUCT: return "Product";
            case ItemType::ARTICLE: return "Article";
            default: return "Generic";
        }
    }
};

#endif // ITEM_H 