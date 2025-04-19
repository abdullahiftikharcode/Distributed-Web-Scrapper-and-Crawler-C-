#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <string>

// Function to make an HTTP GET request
std::string http_get(const std::string& hostname, const std::string& resource_path);

// Helper function to separate HTTP headers from the body
std::string extract_body(const std::string& response);

#endif // HTTP_CLIENT_H 