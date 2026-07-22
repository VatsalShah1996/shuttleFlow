#include <iostream>
#include <string>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <postgresql/libpq-fe.h>

// Helper function to query student fee status from PostgreSQL
std::string get_fee_status_from_db(const std::string& student_id, const char* db_url) {
    if (!db_url) {
        return "ERROR_NO_DB_URL";
    }

    PGconn* conn = PQconnectdb(db_url);
    if (PQstatus(conn) != CONNECTION_OK) {
        std::cerr << "Database connection failed: " << PQerrorMessage(conn) << std::endl;
        PQfinish(conn);
        return "DB_CONNECTION_ERROR";
    }

    // Prepare SQL query with parameter to prevent SQL injection
    const char* paramValues[1] = { student_id.c_str() };
    PGresult* res = PQexecParams(
        conn,
        "SELECT fee_status FROM students WHERE student_id = $1;",
        1,       // 1 parameter
        NULL,    // Let backend deduce parameter type
        paramValues,
        NULL,    // Param lengths (not needed for text)
        NULL,    // Param formats (text)
        0        // Result format (text)
    );

    std::string status = "NOT_FOUND";
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        status = PQgetvalue(res, 0, 0);
    }

    PQclear(res);
    PQfinish(conn);
    return status;
}

// Ensure students table exists and seed initial data
void init_database(const char* db_url) {
    if (!db_url) return;

    PGconn* conn = PQconnectdb(db_url);
    if (PQstatus(conn) == CONNECTION_OK) {
        const char* create_table_sql = 
            "CREATE TABLE IF NOT EXISTS students ("
            "  student_id VARCHAR(50) PRIMARY KEY,"
            "  fee_status VARCHAR(20) NOT NULL"
            ");"
            "INSERT INTO students (student_id, fee_status) VALUES ('STU001', 'Paid'), ('STU002', 'Pending') ON CONFLICT DO NOTHING;";

        PGresult* res = PQexec(conn, create_table_sql);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::cerr << "Failed to init table: " << PQerrorMessage(conn) << std::endl;
        } else {
            std::cout << "Database initialized successfully." << std::endl;
        }
        PQclear(res);
    }
    PQfinish(conn);
}

int main() {
    const char* db_url = std::getenv("DATABASE_URL");
    if (db_url) {
        init_database(db_url);
    } else {
        std::cout << "WARNING: DATABASE_URL not set. Running without DB." << std::endl;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("Socket failed");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        return 1;
    }

    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        return 1;
    }

    std::cout << "C++ Server listening on port 8080 with PostgreSQL..." << std::endl;

    while (true) {
        int new_socket;
        int addrlen = sizeof(address);
        if ((new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue;
        }

        char buffer[1024] = {0};
        read(new_socket, buffer, 1024);
        std::string request(buffer);

        std::string response_body;
        std::string status_line = "HTTP/1.1 200 OK\r\n";

        // Route: /get-fee-status/{id}
        std::string route_prefix = "GET /get-fee-status/";
        size_t pos = request.find(route_prefix);

        if (pos != std::string::npos) {
            size_t start_id = pos + route_prefix.length();
            size_t end_id = request.find(" ", start_id);
            std::string student_id = request.substr(start_id, end_id - start_id);

            std::string fee_status = get_fee_status_from_db(student_id, db_url);

            if (fee_status != "NOT_FOUND" && fee_status != "ERROR_NO_DB_URL") {
                response_body = "{\"student_id\":\"" + student_id + "\",\"fee_status\":\"" + fee_status + "\"}";
            } else {
                status_line = "HTTP/1.1 404 Not Found\r\n";
                response_body = "{\"error\":\"Student not found\"}";
            }
        } else {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            response_body = "{\"error\":\"Invalid Endpoint\"}";
        }

        std::ostringstream http_response;
        http_response << status_line
                      << "Content-Type: application/json\r\n"
                      << "Access-Control-Allow-Origin: *\r\n"
                      << "Content-Length: " << response_body.length() << "\r\n"
                      << "\r\n"
                      << response_body;

        std::string response_str = http_response.str();
        send(new_socket, response_str.c_str(), response_str.length(), 0);
        close(new_socket);
    }

    close(server_fd);
    return 0;
}