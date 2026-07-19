# Stage 1: Compile the C++ binary using GCC
FROM gcc:latest as builder
WORKDIR /app
COPY main.cpp .
RUN g++ -O3 main.cpp -o backend_app

# Stage 2: Create a secure runtime container environment
FROM debian:stable-slim
WORKDIR /app
COPY --from=builder /app/backend_app .

# Expose port 8080 as a fallback default
EXPOSE 8080

# Execute the application binary
CMD ["./backend_app"]