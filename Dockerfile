# Step 1: Build environment
FROM ubuntu:22.04 AS builder

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install g++, cmake, and PostgreSQL development headers
RUN apt-get update && apt-get install -y \
    g++ \
    cmake \
    libpq-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy C++ source files
COPY . .

# Compile C++ code with libpq linked (-lpq)
RUN g++ -O3 main.cpp -o server -lpq

# Step 2: Minimal runtime environment
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libpq5 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy compiled executable from builder stage
COPY --from=builder /app/server .

EXPOSE 8080

CMD ["./server"]