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
    int clientSocket;
    struct sockaddr_in serverAddr;
    char *sendbuf;
    char *recvbuf;
    int iDataNum = -1;
    int access = 0;

    if(argc <2)
        return -1;
    size_t size = atoi(argv[1]);
    sendbuf = malloc(size);
    recvbuf = malloc(size);
#ifdef TCP
    clientSocket = socket(AF_INET, SOCK_STREAM, 0);
#else
    clientSocket = socket(AF_INET, SOCK_DGRAM, 0);
#endif
    if (clientSocket < 0) {
        perror("socket");
        return 1;
 
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = inet_addr("10.192.168.120");
    socklen_t addr_len = sizeof(serverAddr);
#ifdef TCP
    if (connect(clientSocket, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0) 
    {
        perror("connect");
        return 1;
    }
    printf("连接到主机...\n");
#endif
    snprintf(sendbuf, size, "hello world hello");
    while (1) {
        if (access == 10)
            snprintf(sendbuf, 5, "quit");
#ifdef TCP
        send(clientSocket, sendbuf, size, 0);
#else
        sendto(clientSocket, sendbuf, size , 0, (struct sockaddr*)&serverAddr ,&addr_len);
#endif
        if (access == 10)
            break;
       
        printf("读取消息:");
        recvbuf[0] = '\0';
#ifdef TCP
        iDataNum = recv(clientSocket, recvbuf, size, 0);
#else
        recvfrom(clientSocket , recvbuf , size, 0 ,(struct sockaddr*)&serverAddr ,&addr_len);
#endif
        printf("%s %d\n", recvbuf,iDataNum);
        access++;
    }
 
    close(clientSocket);
    return 0;
 
}
