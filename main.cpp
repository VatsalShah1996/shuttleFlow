#include <iostream>
#include <string>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <postgresql/libpq-fe.h>

// Helper to sanitize simple string outputs for JSON
std::string escape_json(const std::string& input) {
    std::string output = "";
    for (char c : input) {
        if (c == '"') output += "\\\"";
        else if (c == '\\') output += "\\\\";
        else output += c;
    }
    return output;
}

// Ensure database table has all required fields and seed initial data
void init_database(const char* db_url) {
    if (!db_url) return;

    PGconn* conn = PQconnectdb(db_url);
    if (PQstatus(conn) == CONNECTION_OK) {
        const char* create_table_sql = 
            "CREATE TABLE IF NOT EXISTS students ("
            "  student_id VARCHAR(50) PRIMARY KEY,"
            "  name VARCHAR(100) NOT NULL,"
            "  surname VARCHAR(100) NOT NULL,"
            "  phone VARCHAR(20) NOT NULL,"
            "  centre VARCHAR(100) NOT NULL,"
            "  batch VARCHAR(100) NOT NULL,"
            "  fee_status VARCHAR(20) NOT NULL"
            ");"
            "INSERT INTO students (student_id, name, surname, phone, centre, batch, fee_status) "
            "VALUES "
            "  ('STU001', 'Rahul', 'Sharma', '+91 9876543210', 'Main Arena', 'Morning 7-9 AM', 'Paid'),"
            "  ('STU002', 'Vatsal', 'Patel', '+91 9123456789', 'North Hub', 'Evening 5-7 PM', 'Pending') "
            "ON CONFLICT DO NOTHING;";

        PGresult* res = PQexec(conn, create_table_sql);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::cerr << "Failed to init table: " << PQerrorMessage(conn) << std::endl;
        } else {
            std::cout << "Database initialized with extended fields successfully." << std::endl;
        }
        PQclear(res);
    } else {
        std::cerr << "Failed to connect to DB for init: " << PQerrorMessage(conn) << std::endl;
    }
    PQfinish(conn);
}

// Query all students or search by search_term (Name/Surname/Phone/ID)
std::string get_students_json(const char* db_url, const std::string& search_term = "") {
    if (!db_url) return "{\"error\":\"NO_DB_URL\"}";

    PGconn* conn = PQconnectdb(db_url);
    if (PQstatus(conn) != CONNECTION_OK) {
        PQfinish(conn);
        return "{\"error\":\"DB_CONNECTION_ERROR\"}";
    }

    PGresult* res = nullptr;
    if (search_term.empty()) {
        const char* sql = "SELECT student_id, name, surname, phone, centre, batch, fee_status FROM students ORDER BY name ASC;";
        res = PQexec(conn, sql);
    } else {
        std::string wildcard_search = "%" + search_term + "%";
        const char* paramValues[1] = { wildcard_search.c_str() };
        const char* sql = 
            "SELECT student_id, name, surname, phone, centre, batch, fee_status FROM students "
            "WHERE name ILIKE $1 OR surname ILIKE $1 OR phone ILIKE $1 OR student_id ILIKE $1 "
            "ORDER BY name ASC;";
        res = PQexecParams(conn, sql, 1, NULL, paramValues, NULL, NULL, 0);
    }

    std::ostringstream json_out;
    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        int rows = PQntuples(res);
        json_out << "[";
        for (int i = 0; i < rows; ++i) {
            json_out << "{"
                     << "\"student_id\":\"" << escape_json(PQgetvalue(res, i, 0)) << "\","
                     << "\"name\":\"" << escape_json(PQgetvalue(res, i, 1)) << "\","
                     << "\"surname\":\"" << escape_json(PQgetvalue(res, i, 2)) << "\","
                     << "\"phone\":\"" << escape_json(PQgetvalue(res, i, 3)) << "\","
                     << "\"centre\":\"" << escape_json(PQgetvalue(res, i, 4)) << "\","
                     << "\"batch\":\"" << escape_json(PQgetvalue(res, i, 5)) << "\","
                     << "\"fee_status\":\"" << escape_json(PQgetvalue(res, i, 6)) << "\""
                     << "}";
            if (i < rows - 1) json_out << ",";
        }
        json_out << "]";
    } else {
        json_out << "[]";
    }

    PQclear(res);
    PQfinish(conn);
    return json_out.str();
}

int main() {
    const char* db_url = std::getenv("DATABASE_URL");
    if (db_url) {
        init_database(db_url);
    } else {
        std::cout << "WARNING: DATABASE_URL not set." << std::endl;
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

    if (listen(server_fd, 5) < 0) {
        perror("Listen failed");
        return 1;
    }

    std::cout << "C++ Server listening on port 8080 with full student schema..." << std::endl;

    while (true) {
        int new_socket;
        int addrlen = sizeof(address);
        if ((new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue;
        }

        char buffer[2048] = {0};
        read(new_socket, buffer, 2048);
        std::string request(buffer);

        std::string response_body;
        std::string status_line = "HTTP/1.1 200 OK\r\n";

        // Route 1: GET /students  OR  GET /students?search=Rahul
        if (request.find("GET /students") != std::string::npos) {
            std::string search_term = "";
            size_t qpos = request.find("GET /students?search=");
            if (qpos != std::string::npos) {
                size_t start = qpos + std::string("GET /students?search=").length();
                size_t end = request.find(" ", start);
                search_term = request.substr(start, end - start);
            }
            response_body = get_students_json(db_url, search_term);
        }
        // Route 2: GET /get-fee-status/{id} (Legacy backward-compatible endpoint)
        else if (request.find("GET /get-fee-status/") != std::string::npos) {
            size_t pos = request.find("GET /get-fee-status/");
            size_t start_id = pos + std::string("GET /get-fee-status/").length();
            size_t end_id = request.find(" ", start_id);
            std::string student_id = request.substr(start_id, end_id - start_id);

            std::string students_list = get_students_json(db_url, student_id);
            if (students_list != "[]" && students_list.find("error") == std::string::npos) {
                response_body = students_list;
            } else {
                status_line = "HTTP/1.1 404 Not Found\r\n";
                response_body = "{\"error\":\"Student not found\"}";
            }
        } 
        else {
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