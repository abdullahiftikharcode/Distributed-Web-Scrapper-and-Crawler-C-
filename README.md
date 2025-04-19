# Web Scraper Project

A simple web scraper for e-commerce websites, focusing on books.toscrape.com. This project implements a web scraper from scratch, using only the C++ Standard Library and basic Windows socket routines.

## Features

- Raw HTTP client implementation using Windows sockets (Winsock)
- Basic HTML parsing using string manipulation
- Multiple crawling strategies:
  - Queue-based crawling (default) - maintaining pending, processing, and processed URLs
  - Sequential crawling (optional) - following pagination links
- Structured data extraction (book title, price, rating)
- CSV output for easy data analysis
- Runtime performance measurements
- Continuous crawling with keyboard interrupt support

## Distributed Web Scraper

This project also includes a distributed version that allows multiple worker nodes to crawl websites in parallel, coordinated by a central server.

### Features

- Central server that manages multiple worker nodes
- TCP-based communication using only C++ Standard Library and native socket APIs
- Worker registration and unique ID assignment
- Real-time progress tracking and reporting
- Thread-safe data structures for tracking worker state
- Performance metrics including network latency and processing time
- Cross-platform support for Windows and Unix-like systems

## Requirements

- Windows operating system
- C++11 compliant compiler (e.g., g++ via MinGW)
- Make (optional, for building with the provided Makefile)

## Building the Project

### Using Make

If you have Make installed, simply run:

```
make
```

For Windows users, we provide a specific Makefile:

```
make -f Makefile.win
```

This will compile the project and place the executable in the `bin` directory.

### Using the Batch File (Recommended for Windows)

For Windows users, we've provided a batch file for easier compilation:

```
build.bat
```

### Using Direct Build (Alternative)

If you encounter linking issues with the regular build process:

```
direct_build.bat
```

## Socket Test

To verify that your system can properly create and use sockets, run the socket test:

```
test_socket.bat
```

Or if you're using Make:

```
make -f Makefile.win run_test
```

This will compile and run a simple test program that initializes Winsock and creates a socket. If this test succeeds but the main program fails, the issue is likely with the HTTP connection rather than socket initialization.

## Usage

Run the program with optional parameters:

```
bin/webscraper [options] [max_pages]
```

### Options:

- `-h, --help`: Show help message
- `-s, --sequential`: Use sequential crawling (default is queue-based)

### Examples:

- Crawl all available pages using queue-based approach (default):
  ```
  bin/webscraper
  ```

- Crawl up to 5 pages using queue-based approach:
  ```
  bin/webscraper 5
  ```

- Crawl all available pages sequentially:
  ```
  bin/webscraper -s
  ```

- Crawl up to 5 pages sequentially:
  ```
  bin/webscraper -s 5
  ```

## Crawling Strategies

### Queue-Based Crawling (Default)

A comprehensive approach:
1. Maintain three sets of URLs:
   - Pending URLs (queue of URLs to be processed)
   - Processing URLs (URLs currently being processed)
   - Processed URLs (URLs that have been processed)
2. Start with the initial URL in the pending queue
3. For each URL in the queue:
   - Extract book information if it's a category or listing page
   - Extract all hyperlinks from the page
   - Add new, unprocessed links to the pending queue
4. Continue until the queue is empty or maximum pages reached

### Sequential Crawling

The traditional approach that follows pagination links sequentially:
1. Start from the first page
2. Extract book information
3. Find the "next" link
4. Navigate to the next page
5. Repeat until no more "next" links or maximum pages reached

## Behavior

By default, the program will:

1. Continue crawling until either:
   - All pages have been crawled (no more "next" links or empty queue)
   - The specified maximum number of pages has been reached
   - The user presses any key to stop the crawling

2. Print information about each page as it's crawled, including performance metrics

3. Display a sample of the first 5 books found after crawling completes

4. Save all extracted book data to a CSV file named `books.csv` in the current directory

## Project Structure

- `include/` - Header files
  - `Book.h` - Definition of the Book structure
  - `HttpClient.h` - HTTP client interface
  - `HtmlParser.h` - HTML parsing functions
  - `Crawler.h` - Web crawler implementation
  - `config.h` - Platform-specific configurations
- `src/` - Source files
  - `HttpClient.cpp` - Implementation of the HTTP client
  - `HtmlParser.cpp` - Implementation of the HTML parser
  - `Crawler.cpp` - Implementation of the web crawler
  - `main.cpp` - Main program entry point
  - `test_socket.cpp` - Socket functionality test program
- `bin/` - Compiled binary (created during build)
- `obj/` - Object files (created during build)

## Algorithm Analysis

The implementation focuses on efficient string parsing:

- Sequential Crawling:
  - Time Complexity: O(P * N), where P is the number of pages and N is the average HTML size per page
  - Memory Usage: O(B), where B is the total number of books scraped

- Queue-Based Crawling:
  - Time Complexity: O(P * N), where P is the number of pages and N is the average HTML size per page
  - Memory Usage: O(B + U), where B is the total number of books scraped and U is the number of unique URLs
 
Each major operation (HTTP requests, HTML parsing) is timed separately to allow for performance analysis.

## Troubleshooting

### MinGW Socket Issues

If you encounter socket-related errors during compilation or linking:

1. Make sure you're using a recent version of MinGW with Winsock2 support
2. Verify that both `-lwsock32` and `-lws2_32` are included in the linking command
3. If you get "undefined reference" errors for socket functions, try installing MinGW with:
   ```
   mingw-get install mingw32-gcc-g++ mingw32-winpthreads-dev
   ```
4. Run the socket test program to verify basic socket functionality
5. Try a different MinGW distribution like MinGW-W64
6. See the detailed troubleshooting guide in MINGW_TROUBLESHOOTING.md

### Runtime Connection Issues

If the program builds but can't connect to the website:

1. Verify your internet connection
2. Check if the website is accessible from your browser
3. Some networks may block direct socket connections - try using a different network

## Limitations

- No HTTPS support (only plain HTTP)
- Limited error handling for malformed HTML
- No support for JavaScript-rendered content
- Designed specifically for books.toscrape.com structure

### Building the Distributed Version

#### Using Make

```
make -f Makefile.distributed
```

#### Using the Batch File (Windows)

```
build_distributed.bat
```

### Running the Distributed Version

#### Manual Setup

1. Start the server:
   ```
   bin/server
   ```

2. Start one or more workers (in separate terminals):
   ```
   bin/worker -s <server_ip> -p <server_port> -m <max_pages>
   ```
   
#### Automated Setup (Windows)

Use the provided batch file to start the server and multiple workers:

```
run_distributed.bat
```

This will:
1. Start the server on localhost (127.0.0.1) port 9000
2. Launch 3 worker instances, each crawling up to 5 pages

### Worker Command-Line Options

- `-s, --server IP`: Server IP address (default: 127.0.0.1)
- `-p, --port PORT`: Server port (default: 9000)
- `-h, --hostname HOST`: Website hostname to crawl (default: books.toscrape.com)
- `-m, --max-pages N`: Maximum pages for this worker to crawl (default: 5)
- `--help`: Show help message

### Protocol Specification

The distributed system uses a simple text-based protocol:

1. Worker Registration:
   - Worker → Server: `REGISTER`
   - Server → Worker: `ASSIGN_ID:<worker_id>`

2. Progress Updates:
   - Worker → Server: `PROGRESS:<processed_count>`
   - Server → Worker: `ACK`

3. Disconnection:
   - No explicit message, connection is closed

### Architecture

#### Central Server

- Multi-threaded design with one thread per client connection
- Thread-safe worker registry using mutex-protected data structures
- Real-time monitoring and status display
- Graceful handling of worker connections and disconnections

#### Worker

- Connects to the server and registers
- Performs web scraping tasks locally
- Reports progress periodically
- Measures network latency and processing time
- Handles connection errors gracefully

### Design Decisions

1. **TCP Protocol**: Chosen for reliable delivery and connection management
2. **Thread Management**: One thread per client on the server for simplicity and scalability
3. **Data Structures**: Thread-safe map for tracking worker state using mutexes
4. **Error Handling**: Comprehensive error checking for all socket operations
5. **Performance Measurement**: Timing code to measure both network and processing performance

### Distributed vs. Single-Process Performance

The distributed approach offers several advantages:

1. **Horizontal Scaling**: Add more worker nodes to increase throughput
2. **Fault Tolerance**: Workers can fail independently without affecting the whole system
3. **Load Distribution**: Different parts of the website can be crawled by different workers
4. **Reduced IP Blocking**: Requests from multiple IPs reduce the risk of being blocked
5. **Geographic Distribution**: Workers can be placed in different locations to reduce latency 