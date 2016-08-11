#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#define PORT 8080
#define SRV_NAME "NanoWeb"
#define SRV_VER "0.0.1"
#define WEBROOT "./webroot"
#define LOGFILE "./log.txt"

#define INDEX_FILE "index.html"
#define FILE_NOT_FOUND "404.html"
#define FNF_NOT_FOUND "<html><head><title>..</title></head><body>Page not found..</body></html>"

#define HTTP_OK "HTTP/1.0 200 OK\r\n"
#define HTTP_NOT_FOUND "HTTP/1.0 404 NOT FOUND\r\n"
#define HTTP_TYPE_HTML "content-type: text/html\r\n"
#define HTTP_TYPE_CSS "content-type: text/css\r\n"
#define HTTP_SRV_VER "Server: NanoWeb webserver\r\n\r\n"

static std::string Webroot;
static int sockfd;
static std::ofstream SRV_LOG_FILE;

void socket_setup();
void handle_incoming_connections();
void open_log_file();
void strip_input(std::string &res_file);
void send_file(int sock_con, std::string &res_file, FILE *file);
void send_file_not_found(int sock_con);
void handle_request(int, struct sockaddr_in*);
void read_line(const int&, char *, int);
void fatal(const char*);
void write_srv_log(std::ostream &, const std::string&);

void write_srv_log(std::ostream &out, const std::string &msg)
{
    out << msg;
    out.flush();
}

void fatal(const char *err)
{
    std::cout << err << std::endl;
    write_srv_log(SRV_LOG_FILE, std::string(err) + "\n");
    exit(1);
}

void strip_input(std::string &res_file)
{
    std::size_t dir_trav = res_file.find("../");
    while(dir_trav != std::string::npos) {
        res_file.erase(dir_trav, 3);
        dir_trav = res_file.find("../");
    }
}

void read_line(const int &sock_con, char *request, int max_len)
{
    char *ptr = request;
    int eol_match = 0;
    while(recv(sock_con, ptr++, 1, 0) == 1 && --max_len > 0) {
        if(*(ptr-1) == "\r\n"[eol_match]) {
            eol_match++;
            if(eol_match == 2) {
                *(ptr-2) = '\0';
                break;
            }
        } else {
            eol_match = 0;
        }
    }
}

void open_log_file()
{
    SRV_LOG_FILE.open(LOGFILE, 1);
    if(SRV_LOG_FILE.fail())
        fatal("Failed to open file for logging!");

    write_srv_log(SRV_LOG_FILE, std::string(SRV_NAME) + " " + std::string(SRV_VER) + " is now running..\n");
}

void send_file(int sock_con, std::string &res_file, FILE *file)
{
    write_srv_log(SRV_LOG_FILE, "200 OK\n");
    send(sock_con, HTTP_OK, 17, 0);

    if(res_file.find(".css") != std::string::npos)
        send(sock_con, HTTP_TYPE_CSS, 24, 0);
    else if(res_file.find(".html") != std::string::npos)
        send(sock_con, HTTP_TYPE_HTML, 25, 0);

    send(sock_con, HTTP_SRV_VER, 29, 0);

    fseek(file, 0, SEEK_END);
    size_t length = ftell(file);
    fseek(file, 0, SEEK_SET);
    char buffer[length];
    fread(buffer, length, 1, file);

    send(sock_con, buffer, length, 0);
}

void send_file_not_found(int sock_con)
{
    write_srv_log(SRV_LOG_FILE, "404 Not Found\n");
    send(sock_con, HTTP_NOT_FOUND, 24, 0);
    send(sock_con, HTTP_SRV_VER, 29, 0);

    std::ifstream err_file(Webroot + "/" + FILE_NOT_FOUND);
    if(err_file) {
        std::string line;
        while(std::getline(err_file, line))
            send(sock_con, line.c_str(), line.size(), 0);
    } else {
        send(sock_con, FNF_NOT_FOUND, 72, 0);
    }
    err_file.close();
}

void handle_request(int sock_con, struct sockaddr_in *client_addr)
{
    char request[512], *ptr;
    read_line(sock_con, request, 512);

    write_srv_log(SRV_LOG_FILE, "Request from " + std::string(inet_ntoa(client_addr->sin_addr)) +
            ":" + std::to_string(ntohs(client_addr->sin_port)) + " '" + std::string(request) + "'\n");

    ptr = std::strstr(request, " HTTP/");
    if(ptr != NULL)
    {
        *ptr = 0;
        ptr = NULL;
        if(std::strncmp(request, "GET ", 4) == 0)
            ptr = request+4;
        if(std::strncmp(request, "HEAD ", 5) == 0)
            ptr = request+5;

        if(ptr != NULL)
        {
            std::string res_file(ptr);
            if(res_file[res_file.size()-1] == '/')
                res_file += INDEX_FILE;

            strip_input(res_file);
            res_file = Webroot + res_file;

            write_srv_log(SRV_LOG_FILE, "Opening '" + res_file + "' ");
            FILE *req_file = fopen(res_file.c_str(), "rb");
            if(req_file)
            {
                send_file(sock_con, res_file, req_file);
                fclose(req_file);
            }
            else
            {
                send_file_not_found(sock_con);
            }
        } else
            write_srv_log(SRV_LOG_FILE, "UNKNOWN REQUEST\n");
    }
}

void handle_incoming_connections()
{
    int sock_con;
    struct sockaddr_in client_addr;
    socklen_t sin_size = sizeof(struct sockaddr_in);

    while(1)
    {
        sock_con = accept(sockfd, (struct sockaddr *)&client_addr, &sin_size);
        if(sock_con != -1)
        {
            handle_request(sock_con, &client_addr);
            shutdown(sock_con, SHUT_RDWR);
            close(sock_con);
        }
    }
}

void socket_setup()
{
    int _true = 1;
    struct sockaddr_in host_addr;

    if((sockfd = socket(PF_INET, SOCK_STREAM, 0)) == -1)
        fatal("Failed to open socket.");

    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &_true, sizeof(int)) == -1)
        fatal("Setting socket option SO_REUSEADDR failed.");

    host_addr.sin_family = AF_INET;
    host_addr.sin_port = htons(PORT);
    host_addr.sin_addr.s_addr = 0;
    std::memset(&(host_addr.sin_zero), '\0', 8);

    if(bind(sockfd, (struct sockaddr *)&host_addr, sizeof(struct sockaddr)) == -1)
        fatal("Binding to socket failed!");

    if(listen(sockfd, 5) == -1)
        fatal("Listening on socket failed!");
}

int main(int argc, char *argv[])
{
    socket_setup();
    open_log_file();

    if(argc == 2)
    {
        Webroot = argv[1];
    }
    else
    {
        Webroot = "./webroot";
    }

    handle_incoming_connections();
    return 0;
}
