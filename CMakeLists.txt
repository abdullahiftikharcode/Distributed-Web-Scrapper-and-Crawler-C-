cmake_minimum_required(VERSION 3.10)
project(WebScraper)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Threads REQUIRED)

# Common source files used by both targets
set(COMMON_SOURCES
    src/HtmlParser.cpp
    src/HttpClient.cpp
    src/Crawler.cpp
)

# Add include directories
include_directories(include)

# Create server executable (without main.cpp)
add_executable(server 
    src/server.cpp
    ${COMMON_SOURCES}
)

# Link libraries for server
target_link_libraries(server PRIVATE Threads::Threads)

# Create CLI executable
add_executable(webscraper 
    src/main.cpp
    ${COMMON_SOURCES}
)

# Link libraries for CLI
target_link_libraries(webscraper PRIVATE Threads::Threads)

# Copy frontend.html to build directory during configuration
file(COPY ${CMAKE_SOURCE_DIR}/frontend.html DESTINATION ${CMAKE_BINARY_DIR})

# Also add a post-build command to ensure it's there
add_custom_command(
    TARGET server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/frontend.html
        $<TARGET_FILE_DIR:server>/frontend.html
    COMMENT "Copying frontend.html to build directory"
)

# Print debug information
message(STATUS "Source directory: ${CMAKE_SOURCE_DIR}")
message(STATUS "Binary directory: ${CMAKE_BINARY_DIR}")
message(STATUS "Current binary directory: ${CMAKE_CURRENT_BINARY_DIR}") 