FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy source files
COPY . .

# Build the application
RUN mkdir -p build && cd build && \
    cmake .. && \
    make

# Expose ports
EXPOSE 9000 9001

# Run the server
CMD ["./build/server"] 