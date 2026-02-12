#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define BUF_SIZE 4096


/* Sends a simple HTTP GET request */
void send_get_request(int sock, const char *path) {

    char request[512];

    // Create GET request
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.0\r\n"
             "Host: localhost\r\n"
             "\r\n",
             path);

    // Send request to server
    send(sock, request, strlen(request), 0);
}


/* Sends a file using multipart/form-data POST */
void send_post_request(int sock, const char *filename) {

    const char *boundary = "CusBoundary123";
    char buffer[BUF_SIZE];

    // Open file
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        perror("File open error");
        return;
    }

    /*Get File Size */

    fseek(fp, 0, SEEK_END);      // Move pointer to end
    long file_size = ftell(fp);  // Get current position = file size
    fseek(fp, 0, SEEK_SET);      // Move pointer back to start

    // Prepare multipart body start
    char body_start[1024];
    snprintf(body_start, sizeof(body_start),
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
        "Content-Type: application/octet-stream\r\n"
        "\r\n",
        boundary, filename);

    // Prepare multipart body end
    char body_end[256];
    snprintf(body_end, sizeof(body_end),
        "\r\n--%s--\r\n", boundary);

    // Calculate total content length
    long content_length =
        strlen(body_start) +
        file_size +
        strlen(body_end);

    // Create POST header
    char header[1024];
    snprintf(header, sizeof(header),
        "POST /upload HTTP/1.0\r\n"
        "Host: localhost\r\n"
        "Content-Type: multipart/form-data; boundary=%s\r\n"
        "Content-Length: %ld\r\n"
        "\r\n",
        boundary, content_length);

    // Send header and body start
    send(sock, header, strlen(header), 0);
    send(sock, body_start, strlen(body_start), 0);

    // Send file content
    int n;
    while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        send(sock, buffer, n, 0);
    }

    fclose(fp);

    // Send closing boundary
    send(sock, body_end, strlen(body_end), 0);
}


int main(int argc, char *argv[]) {

    if (argc < 3) {
        printf("Usage:\n");
        printf("  %s get /path\n", argv[0]);
        printf("  %s post filename\n", argv[0]);
        return 1;
    }

    // Create TCP socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    // Setup server address
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(SERVER_PORT);
    server.sin_addr.s_addr = inet_addr(SERVER_IP);

    // Connect to server
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("Connection failed");
        return 1;
    }

    // Check command
    if (strcmp(argv[1], "get") == 0) {
        send_get_request(sock, argv[2]);
    }
    else if (strcmp(argv[1], "post") == 0) {
        send_post_request(sock, argv[2]);
    }
    else {
        printf("Invalid command\n");
        close(sock);
        return 1;
    }

    // Read and print server response
    char buf[BUF_SIZE];
    int n;

    while ((n = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }

    close(sock);
    return 0;
}
