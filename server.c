#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>

const char* STATIC_RESPONSE =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Content-Length: 155\r\n"
        "Server: SERVER 2137\r\n"
        "Accept-Ranges: bytes\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<html>\r\n"
        "  <head>\r\n"
        "    <title>An Example Page</title>\r\n"
        "  </head>\r\n"
        "  <body>\r\n"
        "    <p>Hello World, this is a very simple HTML document.</p>\r\n"
        "  </body>\r\n"
        "</html>";

const char* STATIC_RESPONSE_OTHER_ENDPOINT =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Server: SERVER 2137\r\n"
        "Accept-Ranges: bytes\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<html>\r\n"
        "  <head>\r\n"
        "    <title>some other page</title>\r\n"
        "  </head>\r\n"
        "  <body>\r\n"
        "    <p>Hello this is a different page.</p>\r\n"
        "  </body>\r\n"
        "</html>";


#define INPUT_BUFFER_SIZE 4096
#define MAX_EVENTS 1024

void findPath(const char* request, char* target) {
    while(*(request)++ != ' ') {}
    while(*request != ' ') {
        *(target)++ = *(request)++;
    }
    *target = 0x00;
}

const char* pseudoRouter(const char* requestedPath)
{
    if(strcmp("/other", requestedPath) == 0) {
        return STATIC_RESPONSE_OTHER_ENDPOINT;
    }
    else if(strcmp("/vanish", requestedPath) == 0) {
        return NULL;
    }
    else {
        return STATIC_RESPONSE;
    }
}

int main(void) {
    // Tu trzymamy przychodzące dane od przeglądarki.
    char inputBuffer[INPUT_BUFFER_SIZE + 1];
    // Tu trzymamy dane sciezki w przegladarce (endpoint)
    char pathBuffer[1024];
    char ipBuffer[16];

    // Tu tworzymy socket servera IPv4.
    int server = socket(AF_INET, SOCK_STREAM, 0);

    // Tu obsługujemy błąd w tworzeniu socketa servera.
    if(server == -1)
    {
        printf("Error creating a socket server.\n");
        exit(-1);
    }

    int nonBlockServ = fcntl(server, F_SETFL, O_NONBLOCK);

    if (nonBlockServ == -1) {
        printf("Error creating a non-blocking socket server.");
        exit(EXIT_FAILURE);
    }

    // Tu przypisujemy adres IP i port do servera.
    struct sockaddr_in sai;
    sai.sin_addr.s_addr = inet_addr("127.0.0.1"); // IP na którym tworzymy server
    sai.sin_family = AF_INET;                       // IPv4
    sai.sin_port = htons(2137);                     // Port htons(port)
    memset(sai.sin_zero, 0, 8);                     // Padding - zawsze ustaw na 0.

    int ret = bind(server, (struct sockaddr*)&sai, sizeof(sai));

    // Tu obsługujemy bład przypisania IP i portu do servera.
    // WAŻNE żeby to obsłużyć bo ta operacja często może sie nie udać.
    if(ret != 0)
    {
        printf("Error binding server socket.\n");
        exit(-1);
    }

    // Pozwalamy na połączenia przychodzące do naszego servera.
    // (Ustawiamy stan na LISTENING)
    // Te 1024 to backlog czyli kolejka oczekujących połączeń na zaakceptowanie.
    ret = listen(server, 1024);

    // Obsługa błędu przy ustawianiu stanu socketa.
    if(ret != 0)
    {
        printf("Error listening to a server socket.\n");
        exit(-1);
    }

    struct epoll_event ev;
    struct epoll_event events[MAX_EVENTS];

    int epollfd = epoll_create1(0);

    if (epollfd == -1) {
        printf("Error creating epoll instance.\n");
        exit(EXIT_FAILURE);
    }

    ev.events = EPOLLIN;
    ev.data.fd = server;

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, server, &ev) == -1) {
        printf("Error adding an entry to a list of intrests of epoll file descriptors.");
        exit(EXIT_FAILURE);
    }

    // Obsługa połączeń przychodzących.
    // Pętla nieskończona po to żeby nasz server się nie wyłączył po obsłużeniu jednego klienta.
    while(1) {
        int nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);

        if (nfds == -1) {
            printf("Error epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == server) {
                struct sockaddr_in clientData;
                socklen_t size = sizeof(clientData);

                // Akceptujemy połączenie przychodzące.
                // Pod client mamy uchwyt do clienta.
                // Tego uchwytu używamy np. żeby coś do niego wysłać albo coś odebrać.
                int client = accept(server, (struct sockaddr *) &clientData, &size);

                if (client == -1) {
                    printf("Error (accept) client.\n");
                    continue;
                }

                int nonBlockClient = fcntl(client, F_SETFL, O_NONBLOCK);

                if (nonBlockClient == -1) {
                    printf("Error creating a non-blocking socket (client)");
                    close(client);
                    continue;
                }

                ev.data.fd = client;

                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client, &ev) == -1) {
                    printf("epoll_ctl error");
                    close(client);
                }

                printf("Accepted connection from %s:%d\n",
                       inet_ntop(AF_INET, &clientData.sin_addr.s_addr, ipBuffer, 16), ntohs(clientData.sin_port));
            } else {
                // Requesty klientow
                int client = events[i].data.fd;
                ssize_t received = recv(client, inputBuffer, INPUT_BUFFER_SIZE, 0);

                if(received == -1)
                {
                    if(errno != EWOULDBLOCK && errno != EAGAIN)
                    {
                        printf("Error occured on receiving, closing connection %d.\n", i);
                        close(client);
                    }
                    continue;
                } else {
                    inputBuffer[received] = 0x00;
                    findPath(inputBuffer, pathBuffer);

                    printf("Requested path: %s\n", pathBuffer);

                    const char *response = pseudoRouter(pathBuffer);

                    if (response == NULL) {
                        shutdown(client, SHUT_RDWR);
                        close(client);
                    } else {
                        ssize_t sent = send(client, response, strlen(response), MSG_NOSIGNAL);

                        if (sent == -1) {
                            if(errno != EWOULDBLOCK && errno != EAGAIN)
                            {
                                printf("Error occured on sending, closing connection %d.\n", i);
                                close(client);
                            }
                            continue;
                        }
                        printf("Sent %zu bytes to client.\n", sent);
                        shutdown(client, SHUT_RDWR);
                        close(client);
                        printf("Closed connection on descriptor %d\n\n", client);
                    }
                }
            }
        }
    }

    close(server);
    return 0;
}