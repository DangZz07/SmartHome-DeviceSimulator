#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string.h>

#pragma comment(lib,"ws2_32.lib")
#define MAXLINE 1024
#define TCP_PORT 5000

void send_command(SOCKET sock, char *cmd){
    char buf[MAXLINE];
    sprintf(buf,"%s\r\n",cmd);
    send(sock,buf,strlen(buf),0);
    int n = recv(sock,buf,MAXLINE-1,0);
    if(n>0){
        buf[n]='\0';
        printf("[Server] %s\n",buf);
    }
}

int main(){
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    SOCKET sock = socket(AF_INET, SOCK_STREAM,0);
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(TCP_PORT);
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if(connect(sock,(struct sockaddr*)&servaddr,sizeof(servaddr))!=0){
        printf("Connect failed\n"); return 1;
    }

    send_command(sock,"LOGIN FAN01 0000");
    send_command(sock,"POWER ABCD1234 ON"); // Token lấy từ LOGIN thực tế
    send_command(sock,"STATUS ABCD1234");
    send_command(sock,"PASSWD ABCD1234 5678");
    send_command(sock,"LOGOUT ABCD1234");

    closesocket(sock);
    WSACleanup();
    return 0;
}
