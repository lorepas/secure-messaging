#include "include/socket.h"

SocketClient::SocketClient(int socketType) {
    this->socketType = socketType;
    this->port = 8080;
    createSocket();
}

void SocketClient::createSocket() {
    if ((this->master_fd = socket(AF_INET, this->socketType, 0)) < 0) {
        throw runtime_error("Socket not created.");
    }
    cout << "Socket correctly created" << endl;
    this->address.sin_family = AF_INET;
    this->address.sin_port = htons(this->port);
    // Convert IPv4 and IPv6 addresses from text to binary form
    if(inet_pton(AF_INET, SERVER, &this->address.sin_addr)<=0) {
        throw runtime_error("Invalid address/ Address not supported");
    }
}




SocketClient::~SocketClient() {
}

void SocketClient::makeConnection() {
    if (connect(master_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Connection Error");
        throw runtime_error("Connection Failed");
    }
}

void SocketClient::sendMessage(string message) {
    if (send(master_fd, message.c_str(), message.length(), 0 ) != message.length()) {
        perror("Send Error");
        throw runtime_error("Send failed");
    }   
}

string SocketClient::receiveMessage() {
    char buffer[1025];

    int n;
    // ssize_t recv(int sockfd, const void *buf, size_t len, int flags);
    if ((n = recv(master_fd, buffer, sizeof(buffer)-1, 0)) <= 0) {
        perror("Receive Error");
        throw runtime_error("Receive failed");
    }
    buffer[n] = '\0';
    return string(buffer);   
}

// ------------------------------------------------------------------------------------

SocketServer::SocketServer(int socketType):SocketClient(socketType) {

    this->port = 8888;
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        this->client_socket[i] = 0;
    }
    
    this->address.sin_addr.s_addr = INADDR_ANY;
    // int opt = true;
    //set master socket to allow multiple connections , 
    //this is just a good habit, it will work without this 
    // if(setsockopt(this->master_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, 
    //       sizeof(opt)) < 0 ) {  
    //     throw runtime_error("Error setting the options");
    // }
    this->serverBind();
    this->listenForConnections();
}

SocketServer::~SocketServer() {
}

void SocketServer::serverBind() {
    if (bind(master_fd, (struct sockaddr *)&address, sizeof(address)) < 0)  {  
        throw runtime_error("Error in binding");
    }  
    cout << "Listening on port: " <<  port << endl;  
}

void SocketServer::listenForConnections() {
    if (listen(master_fd, 3) < 0)  {  
        throw runtime_error("Error in listening");
    }
}

void SocketServer::initSet() {
    //clear the socket set 
    FD_ZERO(&readfds);  
     
    //add master socket to set 
    FD_SET(master_fd, &readfds);  
    max_sd = master_fd;  
    
    //add child sockets to set 
    for (int i = 0 ; i < MAX_CLIENTS; i++)  {  
        //socket descriptor 
        sd = client_socket[i];  
        //if valid socket descriptor then add to read list 
        if(sd > 0) FD_SET( sd , &readfds);  
        //highest file descriptor number, need it for the select function 
        if(sd > max_sd) max_sd = sd;  
    }
    addrlen = sizeof(address);
}

bool SocketServer::isMasterSet() {
    return FD_ISSET(master_fd, &readfds);
}

void SocketServer::selectActivity() {
    activity = select( max_sd + 1 , &readfds , NULL , NULL , NULL);  
       
    if ((activity < 0) && (errno!=EINTR)) {  
        throw runtime_error("Error in the select function");
    } 
}

void SocketServer::acceptNewConnection() {
    int new_socket;

    if ((new_socket = accept(master_fd, 
        (struct sockaddr *)&address, (socklen_t*)&addrlen))<0) {
        perror("accept");  
        throw runtime_error("Failure on accept");
    }  
    
    //inform user of socket number - used in send and receive commands
    cout << "--------------------------------" << endl;
    cout << "New connection incoming" << endl;
    cout << "Socket fd is \t" << new_socket << endl;
    cout << "IP: \t\t" <<  inet_ntoa(address.sin_addr) << endl;
    cout << "Port: \t\t" << ntohs(address.sin_port) << endl;
    cout << "--------------------------------" << endl << endl;
    //send new connection greeting message 
    string message = "Hi, i'm the server";
    if(send(new_socket, message.c_str(), message.length(), 0) != message.length()) {  
        throw("Error sending the greeting message");
    }  
    cout << "Welcome message sent successfully to " << new_socket << endl;
    //add new socket to array of sockets 
    for (int i = 0; i < MAX_CLIENTS; i++)  {  
        //if position is empty 
        if(client_socket[i] == 0)  {  
            client_socket[i] = new_socket;  
            cout << "Adding to list of sockets in position " << i << endl;
            break;  
        }  
    } 
}

void SocketServer::readMessageOnOtherSockets() {
    for (int i = 0; i < MAX_CLIENTS; i++)  {  
        int sd = client_socket[i]; 
        if (FD_ISSET( sd , &readfds)) {  
            //Check if it was for closing , and also read the 
            //incoming message 
            int valread;
            if ((valread = read( sd , buffer, 1024)) == 0)  {  
                //Somebody disconnected , get his details and print 
                getpeername(sd , (struct sockaddr*)&address , (socklen_t*)&addrlen);
                cout << "----Host disconnected----" << endl;
                cout << "IP: \t\t" << inet_ntoa(address.sin_addr) << endl;
                cout << "Port: \t\t" << ntohs(address.sin_port) << endl;
                cout << "-------------------------" << endl << endl;
                //Close the socket and mark as 0 in list for reuse 
                close(sd);  
                client_socket[i] = 0;
            } 
            //Echo back the message that came in 
            else {  
                //set the string terminating NULL byte on the end 
                //of the data read 
                buffer[valread] = '\0';  
                send(sd , buffer , strlen(buffer) , 0 );
            }  
        }  
    }  
}