#include "include/client.h"

int Client::login(string pwd) {
    // Generate nonce
    Crypto crypto(NULL); // Refactor
    string nonce_client = convert(crypto.generateNonce());
    try {
        string helloMessage = "hello" + nonce_client;
        sendMessage(helloMessage);
        string message_received = readMessage();
        string nonce_received = extractClientNonce(message_received, nonce_client.length());
        string nonce_server = extractServerNonce(message_received, nonce_client.length());
        if(nonce_client.compare(nonce_received) != 0) {
            throw "Login Error: The freshness of the message is not confirmed";
        }
        string requestCertificateMessage = (char)OP_CERTIFICATE_REQUEST + nonce_server + nonce_client;
        sendMessage(requestCertificateMessage);
        string certificate = readMessage();
        bool verification = verifyCertificate();
        return 0;
    } catch(const char* ex){
        throw "Login Error: " + string(ex);
    }
}

void Client::sendMessage(string message) {
    int sock = 0;
    struct sockaddr_in serv_addr;

    char buffer[1024] = {0};
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        cerr << "\n Socket creation error \n" << endl;
        throw "Socket creation error";
    }
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    
    // Convert IPv4 and IPv6 addresses from text to binary form
    if(inet_pton(AF_INET, SERVER, &serv_addr.sin_addr)<=0) {
        throw "\nInvalid address/ Address not supported \n";
    }
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        throw "\nConnection Failed \n";
    }
    send(sock, message.c_str(), message.length(), 0 );
    if(read(sock,buffer,1024) == -1) {
        // TODO: controllare se vogliamo usare errno.h
        throw "\nError in response\n";
    }
}

string Client::convert(unsigned char* value) {
    string s;
    for (size_t i = 0; i < sizeof(value); i++){
        s.append(1, static_cast<char>(value[i]));
    }
    return s;
}

string Client::readMessage() {
    int server_fd, new_socket, valread;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};
       
    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port 8080
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons( PORT );
       
    // Forcefully attaching socket to the port 8080
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address))<0) {
        throw "bind failed";
    }
    if (listen(server_fd, 3) < 0) {
        throw "listen";
    }
    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen))<0) {
        throw "accept";
    }
    valread = read(new_socket, buffer, 1024);
    return string(buffer);
}

bool Client::verifyCertificate() {
    //TODO: implement
    return true;
}

string extractClientNonce(string message, size_t clientNonceLen) {
    if (message.length() < 5 + clientNonceLen) throw "Uncorrect format of the message received";
    string clientNonce = message.erase(0, 5); // remove the hello message
    return clientNonce.substr(0, clientNonceLen - 1);
}

string extractServerNonce(string message, size_t clientNonceLen) {
    if (message.length() < 5 + clientNonceLen) throw "Uncorrect format of the message received";
    string serverNonce = message.erase(0, 5); // remove the hello message
    return serverNonce.erase(0, clientNonceLen);
}