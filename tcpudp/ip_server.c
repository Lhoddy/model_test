#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#define SERVER_PORT 6666
#define TCP
int main(int argc,char *argv[]) 
{
    int serverSocket;
    struct sockaddr_in server_addr;
    int addr_len = sizeof(server_addr);
    int client;
    char *recvbuf;
    int iDataNum = -1; 

    if(argc <2)
        return -1;
    size_t size = atoi(argv[1]);
    recvbuf = malloc(size);
#ifdef TCP
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
#else
    serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
#endif
    if (serverSocket < 0) {
        perror("socket");
        return 1;
    }

    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr("10.192.168.120");//htons(INADDR_ANY);

    if (bind(serverSocket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        return 1;
    }
 
#ifdef TCP
    if (listen(serverSocket, 5) < 0) {
        perror("listen");
        return 1;
    }
    printf("监听端口: %d\n", SERVER_PORT);
#endif
     struct sockaddr_in clientAddr;
#ifdef TCP
    while (1) {
        client = accept(serverSocket, (struct sockaddr *) &clientAddr, (socklen_t *) &addr_len);
        if (client < 0) {
            perror("accept");
            continue;
        }
        printf("等待消息...\t");
        printf("IP is %s\t", inet_ntoa(clientAddr.sin_addr));
        printf("Port is %d\n", htons(clientAddr.sin_port));
 
        while (1) {
            iDataNum = recv(client, recvbuf, size, 0);
            if (iDataNum < 0) {
                perror("recv null");
                continue;
            }
            printf("recv = %s\n", recvbuf);
            if (strcmp(buffer, "quit") == 0)
                break;
            send(client, recvbuf, size, 0);
        }
 
    }
#else
    while (1) {
        recvfrom(serverSocket , recvbuf , size, 0 ,(struct sockaddr*)&clientAddr ,&addr_len);
        printf("recv = %s\n", recvbuf);
        if (strcmp(recvbuf, "quit") == 0)
            break;

        sendto(serverSocket, recvbuf, size , 0, (struct sockaddr*)&clientAddr ,addr_len);
    }
#endif
    close(serverSocket);
    return 0;
}