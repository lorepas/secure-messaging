#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <string>
#include <unistd.h>
#include "include/server.h"
#include "include/socket.h"

#define PORT 8080

int main(int argc, char* const argv[]) {
    try {
        SocketServer serverSocket = SocketServer(SOCK_STREAM); //TCP
        while(true) {
            serverSocket.initSet();
            serverSocket.selectActivity();
            if(serverSocket.isFDSet(serverSocket.getMasterFD())) {
                serverSocket.acceptNewConnection();
            } else {
                for (unsigned int i = 0; i < MAX_CLIENTS; i++)  {  
                    int sd = serverSocket.getClient(i); 
                    if (serverSocket.isFDSet(sd)) {  
                        //Check if it was for closing , and also read the 
                        //incoming message 
                        string messageReceived = serverSocket.receiveMessage(sd);
                        if (messageReceived.length() == 0)  { 
                            //Somebody disconnected , get his details and print 
                            serverSocket.disconnectHost(sd, i);
                        } 
                        //Echo back the message that came in 
                        else {
                            cout << "Received message from peer: " << messageReceived << endl;
                        }  
                    }  
                }
            }
        }
    } catch(const exception& e) {
        cerr << e.what() << '\n';
    }
    return 0;
}

