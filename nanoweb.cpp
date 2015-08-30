#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

/* * * * * * * * * * * * * * * * * * * * * * * * * * *
 * NOTE : Vulnerable to [ ] -> mark with 'x' after fix 
 *          - directory traversal
 *              [x] remove all occurences of ../
 *
 *          - buffer overflow
 *              [x] explicitly make sure to not exceed
 *                  length of buffer.
 *      
 * * * * * * * * * * * * * * * * * * * * * * * * * * */

#define PORT 8080
#define SRV_NAME "NanoWeb"
#define SRV_VER "0.0.1"
#define WEBROOT "./webroot"
#define LOGFILE "./log.txt"

#define INDEX_FILE "index.html"
#define FILE_NOT_FOUND "404.html"
#define FNF_NOT_FOUND "<html><head><title>..</title></head><body>Page not found..</body></html>"

#define HTTP_NOT_FOUND "HTTP/1.0 404 NOT FOUND\r\n"
#define HTTP_OK "HTTP/1.0 200 OK\r\n"
#define HTTP_TYPE_HTML "content-type: text/html\r\n"
#define HTTP_TYPE_CSS "content-type: text/css\r\n"
#define HTTP_SRV_VER "Server: NanoWeb webserver\r\n\r\n"

std::ofstream SRV_LOG_FILE;

void handle_con(int, struct sockaddr_in*);
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

int main(int argc, char *argv[])
{
    int sockfd, sock_con, _true = 1;
    struct sockaddr_in host_addr, client_addr;
    socklen_t sin_size = sizeof(struct sockaddr_in);

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
    
    SRV_LOG_FILE.open(LOGFILE, 1);
    if(SRV_LOG_FILE.fail())
        fatal("Failed to open file for logging!");

    write_srv_log(SRV_LOG_FILE, std::string(SRV_NAME) + " " + std::string(SRV_VER) + " is now running..\n");

    while(1) {
        sock_con = accept(sockfd, (struct sockaddr *)&client_addr, &sin_size);
        if(sock_con == -1)
            fatal("Failed to accept connection.");

         handle_con(sock_con, &client_addr);
    }

    return 0;
}

void handle_con(int sock_con, struct sockaddr_in *client_addr)
{
    char request[512], *ptr;
    read_line(sock_con, request, 512);

    write_srv_log(SRV_LOG_FILE, "Request from " + std::string(inet_ntoa(client_addr->sin_addr)) + 
                  ":" + std::to_string(ntohs(client_addr->sin_port)) + " '" + std::string(request) + "'\n");
    
    ptr = std::strstr(request, " HTTP/");
    if(ptr == NULL) {
        write_srv_log(SRV_LOG_FILE, "NOT HTTP - Request can not be processed!\n");
    } else {
        *ptr = 0;
        ptr = NULL;
        if(std::strncmp(request, "GET ", 4) == 0)
            ptr = request+4;
        if(std::strncmp(request, "HEAD ", 5) == 0)
            ptr = request+5;
        if(ptr == NULL) {
            write_srv_log(SRV_LOG_FILE, "UNKNOWN REQUEST\n");
        } else {
            std::string res_file(ptr);
            if(res_file[res_file.size()-1] == '/')
                res_file += INDEX_FILE;

            std::size_t dir_trav = res_file.find("../");
            while(dir_trav != std::string::npos) {
                res_file.erase(dir_trav, 3);
                dir_trav = res_file.find("../");
            }
            res_file = WEBROOT + res_file;
        
            write_srv_log(SRV_LOG_FILE, "Opening '" + res_file + "' ");
            
            std::ifstream req_file(res_file);
            if(req_file) {
                write_srv_log(SRV_LOG_FILE, "200 OK\n");
                
                send(sock_con, HTTP_OK, 17, 0);

                if(res_file.find(".css") != std::string::npos)
                    send(sock_con, HTTP_TYPE_CSS, 24, 0);
                else if(res_file.find(".html") != std::string::npos)
                    send(sock_con, HTTP_TYPE_HTML, 25, 0);

                send(sock_con, HTTP_SRV_VER, 29, 0);
                
                std::string line;
                while(std::getline(req_file, line))
                    send(sock_con, line.c_str(), line.size(), 0);
                send(sock_con, "\r\n", 2, 0);
                req_file.close();
            } else {
                write_srv_log(SRV_LOG_FILE, "404 Not Found\n");
                
                send(sock_con, HTTP_NOT_FOUND, 24, 0);
                send(sock_con, HTTP_SRV_VER, 29, 0); 
                
                std::ifstream err_file(std::string(WEBROOT) + "/" + FILE_NOT_FOUND);
                if(err_file) {
                    std::string line;
                    while(std::getline(err_file, line))
                        send(sock_con, line.c_str(), line.size(), 0);
                } else {
                    send(sock_con, FNF_NOT_FOUND, 72, 0); 
                }
                send(sock_con, "\r\n", 2, 0);
                err_file.close();
            }
        }
    } 
    shutdown(sock_con, SHUT_RDWR);
}
