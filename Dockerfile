FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libssl-dev \
    pkg-config \
    libcurl4-openssl-dev \
    tini \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy source files
COPY . .

# Debug: Show contents before build
RUN echo "Contents before build:" && ls -la

# Ensure frontend.html exists in source directory
RUN test -f frontend.html || (echo "frontend.html not found in source directory" && exit 1)

# Build the application
RUN mkdir -p build && cd build && \
    echo "Running CMake..." && \
    cmake .. && \
    echo "Running make..." && \
    make VERBOSE=1 && \
    echo "Build complete. Contents of build directory:" && \
    ls -la && \
    echo "Current directory:" && \
    pwd

# Ensure frontend.html exists in build directory
RUN test -f build/frontend.html || \
    (echo "frontend.html not found in build directory, copying now..." && \
    cp frontend.html build/ && \
    echo "Copied frontend.html to build directory")

# Verify frontend.html exists and is readable
RUN test -f build/frontend.html && test -r build/frontend.html && \
    echo "frontend.html exists and is readable in build directory"

# Create a non-root user
RUN useradd -m -s /bin/bash app_user && \
    chown -R app_user:app_user /app

# Switch to non-root user
USER app_user

# Expose ports
EXPOSE 9000 9001

# Use tini as init
ENTRYPOINT ["/usr/bin/tini", "--"]

# Run the server with debug output (using shell form)
CMD /bin/bash -c 'cd /app/build && \
    echo "Starting server in $PWD" && \
    ls -la && \
    echo "Verifying frontend.html:" && \
    cat frontend.html | head -n 5 && \
    exec ./server' 