#include <winsock2.h> 
#include <ws2tcpip.h> 
#include <iostream> 
int main() { 
   WSADATA wsaData; 
   WSAStartup(MAKEWORD(2, 2), &wsaData); 
   struct sockaddr_in addr; 
   char ip[16] = "127.0.0.1"; 
   if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) { 
       std::cout << "inet_pton not working"; 
   } 
   WSACleanup(); 
   return 0; 
} 
