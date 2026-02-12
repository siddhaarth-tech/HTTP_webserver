#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>

#define PORT 8080                 // Server will listen on port 8080
#define MAX_EVENTS 1024           // Maximum events epoll can handle at once
#define BUFFER_SIZE 16384         // Buffer to store incoming HTTP request

#define WEB_ROOT "/var/www"       // Folder where static website files are stored
#define UPLOAD_PATH "/var/www/uploads"  // Folder where uploaded files are saved


// Sends HTTP 404 response when file or route is not found
void send_404(int fd) {
    const char *msg =
        "HTTP/1.0 404 Not Found\r\n"
        "\r\n"
        "404 Not Found";

    // Sending the response string to client socket
    send(fd, msg, strlen(msg), 0);
}


// Sends HTTP 500 response when server error occurs
void send_500(int fd) {
    const char *msg =
        "HTTP/1.0 500 Internal Server Error\r\n"
        "\r\n"
        "Internal Server Error";

    // Sending error message back to client
    send(fd, msg, strlen(msg), 0);
}


// Sends HTTP 200 OK response along with body content
void send_200(int fd, const char *body) {
    char header[256];

    // Creating HTTP response header
    // Content-Length is required so browser knows how much data to read
    snprintf(header, sizeof(header),
        "HTTP/1.0 200 OK\r\n"
        "Content-Length: %ld\r\n"
        "\r\n",
        strlen(body));

    // First send header, then send body
    send(fd, header, strlen(header), 0);
    send(fd, body, strlen(body), 0);
}


// Handles GET request from client
void handle_get(int fd, const char *path) {

    char full_path[512];

    // If user requests "/", serve index.html file
    if (strcmp(path, "/") == 0)
        snprintf(full_path, sizeof(full_path),
                 "%s/index.html", WEB_ROOT);
    else
        // Otherwise serve requested file inside WEB_ROOT
        snprintf(full_path, sizeof(full_path),
                 "%s%s", WEB_ROOT, path);

    // Open the requested file
    FILE *file = fopen(full_path, "r");
    if (!file) {
        // If file not found, return 404
        send_404(fd);
        return;
    }

    // Move file pointer to end to calculate file size
    fseek(file, 0, SEEK_END);

    // Get file size
    long size = ftell(file);

    // Move pointer back to beginning for reading
    rewind(file);

    // Prepare HTTP header with file size
    char header[256];
    snprintf(header, sizeof(header),
        "HTTP/1.0 200 OK\r\n"
        "Content-Length: %ld\r\n"
        "\r\n", size);

    // Send header
    send(fd, header, strlen(header), 0);

    // Read file content in chunks and send to client
    char buffer[BUFFER_SIZE];
    int n;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0)
        send(fd, buffer, n, 0);

    fclose(file);  // Close file after sending
}


// Extracts Content-Length value from HTTP header
// Needed to know how many bytes to read in POST request
int extract_content_length(char *req) {

    // Search for "Content-Length:" in header
    char *cl = strstr(req, "Content-Length:");
    if (!cl) return 0;

    int len;

    // Read integer value from that line
    sscanf(cl, "Content-Length: %d", &len);

    return len;
}


// Handles POST request (file upload)
void handle_post(int fd, char *request) {

    // Get size of body data
    int content_length = extract_content_length(request);
    if (content_length <= 0) {
        send_500(fd);
        return;
    }

    // Find where HTTP header ends and body starts
    char *body = strstr(request, "\r\n\r\n");
    if (!body) {
        send_500(fd);
        return;
    }
    body += 4;  // Move pointer to actual body start

    // Find boundary string (used in multipart form upload)
    char *boundary_ptr = strstr(request, "boundary=");
    
    char boundary[128];
    sscanf(boundary_ptr, "boundary=%127[^;\r\n]", boundary);

    // Create ending boundary marker
    char end_boundary[150];
    snprintf(end_boundary, sizeof(end_boundary),
             "--%s--", boundary);

    // Find filename inside multipart data
    char *filename_ptr = strstr(body, "filename=\"");
    if (!filename_ptr) {
        send_500(fd);
        return;
    }

    filename_ptr += 10;  // Move pointer after filename="

    char filename[128];
    sscanf(filename_ptr, "%127[^\"]", filename);

    // Find where actual file data begins
    char *file_data = strstr(filename_ptr, "\r\n\r\n");
    if (!file_data) {
        send_500(fd);
        return;
    }
    file_data += 4;

    // Find where file data ends (before boundary)
    char *file_end = strstr(file_data, end_boundary);
    if (!file_end) {
        send_500(fd);
        return;
    }

    // Calculate size of uploaded file
    int file_size = file_end - file_data;

    // Remove trailing newline characters
    while (file_size > 0 &&
           (file_data[file_size - 1] == '\r' ||
            file_data[file_size - 1] == '\n'))
        file_size--;

    // Create full path to save uploaded file
    char save_path[256];
    snprintf(save_path, sizeof(save_path), "%s/%s", UPLOAD_PATH, filename);

    // Open file for writing
    FILE *upload_file = fopen(save_path, "w");
    if (!upload_file) {
        send_500(fd);
        return;
    }

    // Write uploaded data into file
    fwrite(file_data, 1, file_size, upload_file);
    fclose(upload_file);

    // Inform client upload was successful
    send_200(fd, "Upload Successful\n");
}


// Handles connected client socket
void handle_client(int fd) {

    char buffer[BUFFER_SIZE];
    int total = 0;
    int bytes;

    // Keep reading until full HTTP request is received
    while (1) {

        bytes = recv(fd, buffer + total, sizeof(buffer) - total - 1, 0);

        if (bytes <= 0)
            break;

        total += bytes;
        buffer[total] = '\0';

        // Check if header finished
        char *header_end = strstr(buffer, "\r\n\r\n");
        if (header_end) {

            int header_size = header_end - buffer + 4;
            int content_length = extract_content_length(buffer);

            // Stop reading if full body received
            if (total >= header_size + content_length)
                break;
        }

        if (total >= sizeof(buffer) - 1)
            break;
    }

    if (total <= 0)
        return;

    buffer[total] = '\0';

    char method[8], path[256];

    // Extract method (GET/POST) and path 
    sscanf(buffer, "%7s %255s", method, path);

    if (strcmp(method, "GET") == 0)
        handle_get(fd, path);
    else if (strcmp(method, "POST") == 0)
        handle_post(fd, buffer);
    else
        send_404(fd);
}


// Main function: starts the server
int main() {

    // Create TCP socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;

    // Allow port reuse so server can restart quickly
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);   // Convert port to network format
    addr.sin_addr.s_addr = INADDR_ANY;  // Accept connections from any IP

    // Bind socket to port
    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));

    // Start listening for client connections
    listen(server_fd, 100);

    // Create epoll instance for handling multiple clients
    int epoll_fd = epoll_create1(0);

    struct epoll_event event, events[MAX_EVENTS];
    event.events = EPOLLIN;   // Monitor read events
    event.data.fd = server_fd;

    // Add server socket to epoll
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event);

    printf("HTTP Server running on port %d...\n", PORT);

    while (1) {

        // Wait for client events
        int ready = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        for (int i = 0; i < ready; i++) {

            if (events[i].data.fd == server_fd) {

                // New client is connecting
                int client_fd = accept(server_fd, NULL, NULL);

                // Add client socket to epoll
                event.events = EPOLLIN;
                event.data.fd = client_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event);

            } else {

                // Existing client sent data
                int client_fd = events[i].data.fd;

                handle_client(client_fd);

                // Close client after handling request
                close(client_fd);
            }
        }
    }
}
