#include <cstring>
#include <termios.h>
#include "socket.h"
#include "utils.h"

struct ClientContext {
    vector<string> onlineUsers;
    EVP_PKEY *prvKeyClient;
    SocketClient *clientSocket;
    Crypto *crypto;

    ClientContext() {
        clientSocket = new SocketClient(SOCK_STREAM);
        crypto = new Crypto();
    }

    void addOnlineUser(string username) {
        onlineUsers.push_back(username);
    }

    void clearOnlineUsers() {
        onlineUsers.clear();
    }

    bool userIsPresent(string username){
        for(string user : onlineUsers){
            if(user.compare(username) == 0){
                return true;
            }
        }
        return false;
    }
};

void receive(SocketClient *socket, vector<unsigned char> &buffer) {
    std::array<unsigned char, MAX_MESSAGE_SIZE> msg;
    unsigned int size;

    try {
        size = socket->receiveMessage(socket->getMasterFD(), msg.data());
        buffer.insert(buffer.end(), msg.begin(), msg.begin() + size);
    } catch(const exception& e) {
        throw;
    }
}

void send(SocketClient *socket, vector<unsigned char> &buffer) {
    try {
        socket->sendMessage(socket->getMasterFD(), buffer.data(), buffer.size());
        buffer.clear();
    } catch(const exception& e) {
        throw;
    }
}

void receive(SocketClient *socket, Crypto *crypto, vector<unsigned char> &buffer) {
    unsigned char opCode;

    try {
        receive(socket, buffer);
        opCode = buffer.at(0);
        buffer.erase(buffer.begin());
        decrypt(crypto, SERVER_SECRET, buffer);

        if(buffer.at(0) != opCode) {
            cout << "Message tampered" << endl;
            throw runtime_error("Message tampered");
        }
    } catch(const exception& e) {
        throw;
    }
}

void send(SocketClient *socket, Crypto *crypto, vector<unsigned char> &buffer) {
    unsigned char opCode;
    try {
        opCode = buffer.at(0);
        encrypt(crypto, SERVER_SECRET, buffer);
        buffer.insert(buffer.begin(), opCode);
        send(socket, buffer);
    } catch(const exception& e) {
        throw;
    }
}

void setStdinEcho(bool enable = true) {
    struct termios tty;
    tcgetattr(STDIN_FILENO, &tty);
    if(!enable)
        tty.c_lflag &= ~ECHO;
    else
        tty.c_lflag |= ECHO;
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &tty);
}

string readPassword() {
    string password;
    cout << "Insert password: ";
    setStdinEcho(false);
    cin >> password;
    cin.ignore();
    setStdinEcho(true);
    cout << endl;
    return password;
}

string readFromStdout(string message) {
    string value;
    cout << message;
    
    do {
        getline(cin, value);
        if(value.length() == 0) {
            cout << "Insert at least a character." << endl;
            cout << message;
        }
    } while (value.length() == 0);
    
    return value;
}

void authentication(ClientContext ctx, string username) {
    vector<unsigned char> buffer;
    vector<unsigned char> signature;
    array<unsigned char, NONCE_SIZE> nonceClient;
    array<unsigned char, NONCE_SIZE> nonceServer;
    array<unsigned char, MAX_MESSAGE_SIZE> pubKeyDHBuffer;
    array<unsigned char, MAX_MESSAGE_SIZE> tempBuffer;
    EVP_PKEY *pubKeyServer = NULL;
    EVP_PKEY *prvKeyDHClient = NULL;
    EVP_PKEY *pubKeyDHServer = NULL;
    X509 *cert;
    unsigned int tempBufferLen;
    unsigned int pubKeyDHServerLen;
    unsigned int pubKeyDHClientLen;

    try {
        // M1: 0, username, nc
        ctx.crypto->generateNonce(nonceClient.data());
        buffer.push_back(OP_LOGIN);
        append(username, buffer);
        append(nonceClient, NONCE_SIZE, buffer);
        send(ctx.clientSocket, buffer);

        // Receive M2: 0, cert, g^b mod p, ns, <0, g^b mod p, nc > pKs
        receive(ctx.clientSocket, buffer);
        if (buffer.at(0) != OP_LOGIN) {
            // TODO: Handle Error!
            throw runtime_error("Opcode not valid");
        }
        buffer.erase(buffer.begin());
        tempBufferLen = extract(buffer, tempBuffer);
        ctx.crypto->deserializeCertificate(tempBufferLen, tempBuffer.data(), cert);

        if(!ctx.crypto->verifyCertificate(cert)) {
            // TODO: 
            throw runtime_error("Certificate not valid.");
        }

        cout << "The certificate is valid." << endl;

        ctx.crypto->getPublicKeyFromCertificate(cert, pubKeyServer);

        pubKeyDHServerLen = extract(buffer, pubKeyDHBuffer);
        extract(buffer, nonceServer);
        tempBufferLen = extract(buffer, tempBuffer);

        signature.push_back(OP_LOGIN);
        signature.insert(signature.end(), pubKeyDHBuffer.begin(), pubKeyDHBuffer.begin() + pubKeyDHServerLen);
        signature.insert(signature.end(), nonceClient.begin(), nonceClient.end());

        bool signatureVerification = ctx.crypto->verifySignature(tempBuffer.data(), tempBufferLen, signature.data(), signature.size(), pubKeyServer);
        if(!signatureVerification) {
            // TODO: Send Message to other client
            throw runtime_error("Sign verification failed");
        }

        cout << "The signature is correct." << endl;

        ctx.crypto->deserializePublicKey(pubKeyDHBuffer.data(), pubKeyDHServerLen, pubKeyDHServer);

        cout << "Message M2 has been received correctly." << endl;

        // Send M3: 0, g^a mod p, ns, < 0, g^a mod b,  ns>
        buffer.clear();
        buffer.push_back(OP_LOGIN);

        ctx.crypto->keyGeneration(prvKeyDHClient);
        pubKeyDHClientLen = ctx.crypto->serializePublicKey(prvKeyDHClient, pubKeyDHBuffer.data());
        append(pubKeyDHBuffer, pubKeyDHClientLen, buffer);

        signature.clear();
        signature.push_back(OP_LOGIN);
        signature.insert(signature.end(), pubKeyDHBuffer.begin(), pubKeyDHBuffer.begin() + pubKeyDHClientLen);
        signature.insert(signature.end(), nonceServer.begin(), nonceServer.end());
        
        tempBufferLen = ctx.crypto->sign(signature.data(), signature.size(), tempBuffer.data(), ctx.prvKeyClient);
        
        append(tempBuffer, tempBufferLen, buffer);

        send(ctx.clientSocket, buffer);

        cout << "M3 correctly sent." << endl;

        // Receive M4: 
        receive(ctx.clientSocket, buffer);
        printBuffer("Online users encrypted: ", buffer);
        if (buffer.at(0) != OP_LOGIN) {
            // TODO: Handle Error!
            throw runtime_error("Authentication Failed");
        }
        buffer.erase(buffer.begin());
        cout << "Authentication succeeded" << endl;
        
        ctx.crypto->secretDerivation(prvKeyDHClient, pubKeyDHServer, tempBuffer.data());

        printBuffer("O' Secret derivat: ", tempBuffer, DIGEST_LEN);

        ctx.crypto->insertKey(tempBuffer.data(), SERVER_SECRET);

        ctx.crypto->setSessionKey(SERVER_SECRET);

        tempBufferLen = ctx.crypto->decryptMessage(buffer.data(), buffer.size(), tempBuffer.data());

        buffer.clear();
        buffer.insert(buffer.begin(), tempBuffer.begin(), tempBuffer.begin() + tempBufferLen-2);
        buffer.erase(buffer.begin());

        printBuffer("Buffer: ", buffer);

        while(buffer.size() != 0) {
            string name = extract(buffer);
            cout << "Name: " << name << endl;
            ctx.addOnlineUser(name);
        }
        

    } catch(const exception& e) {
        cout << "Error: " << e.what();
        throw;
    }
}

void receiveRequestToTalk(ClientContext ctx, vector<unsigned char> msg) {
    array<unsigned char, NONCE_SIZE> nonce;
    array<unsigned char, NONCE_SIZE> peerNonce;
    array<unsigned char, MAX_MESSAGE_SIZE> tempBuffer;
    array<unsigned char, MAX_MESSAGE_SIZE> keyBuffer;
    array<unsigned char, MAX_MESSAGE_SIZE> keyBufferDH;
    vector<unsigned char> buffer;
    vector<unsigned char> signature;
    unsigned int tempBufferLen = 0;
    unsigned int keyBufferLen = 0;
    unsigned int keyBufferDHLen = 0;
    EVP_PKEY *keyDH = NULL;
    EVP_PKEY *peerKeyDH = NULL;
    EVP_PKEY *peerPubKey = NULL;
    string peerUsername;
    string input;
    bool verify = false;

    try {
        // Receive request
        tempBufferLen = ctx.crypto->decryptMessage(msg.data(), msg.size(), tempBuffer.data());

        if(tempBuffer.at(0) != OP_REQUEST_TO_TALK) {
            errorMessage("Request to talk failed", buffer);
            send(ctx.clientSocket, ctx.crypto, buffer);
            throw runtime_error("Request to talk failed");
        }

        // Get peer username
        buffer.insert(buffer.end(), tempBuffer.begin() + 1, tempBuffer.begin() + tempBufferLen);
        peerUsername = extract(buffer);
        cout << peerUsername << " sent you a request to talk" << endl;

        // Accept or refuse request
        cout << "Do you want to accept the request? (y/n):" << endl;
        while(true) {
            getline(cin, input);
            if(input.length() == 0) {
                cout << "Insert at least a character." << endl;
            } else if(input.compare("y") == 0) {
                cout << "Request accepted" << endl;
                break;
            } else if (input.compare("n") == 0) {
                cout << "Request refused" << endl;
                buffer.clear();
                errorMessage("Request to talk refused", buffer);
                encrypt(ctx.crypto, SERVER_SECRET, buffer);
                buffer.insert(buffer.begin(), OP_ERROR);
                send(ctx.clientSocket, buffer);
                cout << "Request to talk refused" << endl;
                return;
            } else {
                cout << "Insert a valid answer" << endl;
            }       
        }

        // Send nonce and DH public key
        extract(buffer, peerNonce);
        ctx.crypto->generateNonce(nonce.data());
        ctx.crypto->keyGeneration(keyDH);

        buffer.clear();
        buffer.push_back(OP_REQUEST_TO_TALK);

        tempBufferLen = ctx.crypto->serializePublicKey(keyDH, tempBuffer.data());
        append(tempBuffer, tempBufferLen, buffer);
        append(nonce, NONCE_SIZE, buffer);

        signature.insert(signature.end(), tempBuffer.begin(), tempBuffer.begin() + tempBufferLen);
        signature.insert(signature.end(), peerNonce.begin(), peerNonce.end());
        tempBufferLen = ctx.crypto->sign(signature.data(), signature.size(), tempBuffer.data(), ctx.prvKeyClient);
        append(tempBuffer, tempBufferLen, buffer);

        send(ctx.clientSocket, ctx.crypto, buffer);

        // Receive peer's public key
        receive(ctx.clientSocket, ctx.crypto, buffer);
        if(buffer.at(0) == OP_ERROR) {
            buffer.erase(buffer.begin());
            cout << extract(buffer) << endl;
            return;
        }

        buffer.erase(buffer.begin());
        keyBufferDHLen = extract(buffer, keyBufferDH);
        signature.clear();
        signature.insert(signature.end(), keyBufferDH.begin(), keyBufferDH.begin() + keyBufferDHLen);
        signature.insert(signature.end(), nonce.begin(), nonce.end());

        // Extract signed content
        tempBufferLen = extract(buffer, tempBuffer);
        // Extract public key and verify sign
        keyBufferLen = extract(buffer, keyBuffer);
        ctx.crypto->deserializePublicKey(keyBuffer.data(), keyBufferLen, peerPubKey);        
        verify = ctx.crypto->verifySignature(tempBuffer.data(), tempBufferLen, signature.data(), signature.size(), peerPubKey);
        
        if(!verify) {
            errorMessage("Signature not verified", buffer);
            send(ctx.clientSocket, ctx.crypto, buffer);
            cout<<"Signature not verified"<<endl;
            return;
        }

        ctx.crypto->deserializePublicKey(keyBufferDH.data(), keyBufferDHLen, peerKeyDH);
        ctx.crypto->secretDerivation(keyDH, peerKeyDH, tempBuffer.data());
        ctx.crypto->insertKey(tempBuffer.data(), CLIENT_SECRET);

        buffer.clear();
        append("Success", buffer);
        encrypt(ctx.crypto, CLIENT_SECRET, buffer);
        buffer.insert(buffer.begin(), OP_REQUEST_TO_TALK);
        send(ctx.clientSocket, ctx.crypto, buffer);
    } catch(const exception& e) {
        throw;
    }
}

void sendRequestToTalk(ClientContext ctx){
    array<unsigned char, NONCE_SIZE> nonce;
    array<unsigned char, NONCE_SIZE> peerNonce;
    array<unsigned char, MAX_MESSAGE_SIZE> tempBuffer;
    array<unsigned char, MAX_MESSAGE_SIZE> signedPart;
    array<unsigned char, MAX_MESSAGE_SIZE> pubKeyDHBuffer;
    vector<unsigned char> buffer;
    vector<unsigned char> signature;
    unsigned int tempBufferLen = 0;
    unsigned int signedPartLen = 0;
    unsigned int pubKeyDHLen = 0;
    EVP_PKEY *keyDHB = NULL;
    EVP_PKEY *keyDHA = NULL;
    EVP_PKEY *pubKeyB = NULL;

    try {
        // Get user to connect with
        cout << "Who do you want to chat with?" << endl;
        while(true) {
            getline(cin, usernameB);
            if(usernameB.length() == 0){
                cout << "Insert at least a character." << endl;
            } else if(ctx.userIsPresent(usernameB)) {
                break;
            } else {
                cout << "Insert a valid username" << endl;
            }       
        }

        // M1: 2||{2,usr_b, n_a}SA ->
        buffer.push_back(OP_REQUEST_TO_TALK);
        ctx.crypto->generateNonce(nonce.data());
        append(usernameB, buffer);
        append(nonce, NONCE_SIZE, buffer);
        send(ctx.clientSocket, ctx.crypto, buffer);

        // <- M4: 2||{M3||PK_b} SA
        receive(ctx.clientSocket, ctx.crypto, buffer);
        if(buffer.at(0) == OP_ERROR) {
            cout<<extract(buffer)<<endl;
            return;
        }
        buffer.erase(buffer.begin());

        pubKeyDHLen = extract(buffer, pubKeyDHBuffer);
        ctx.crypto->deserializePublicKey(pubKeyDHBuffer.data(), pubKeyDHLen, keyDHB);

        extract(buffer, peerNonce);
        signedPartLen = extract(buffer, signedPart); //extraction of the signed part

        signature.insert(signature.end(), pubKeyDHBuffer.begin(), pubKeyDHBuffer.begin() + pubKeyDHLen);
        signature.insert(signature.end(), nonce.begin(), nonce.end());

        tempBufferLen = extract(buffer, tempBuffer);
        ctx.crypto->deserializePublicKey(pubKeyDHBuffer.data(), pubKeyDHLen, pubKeyB);

        bool signatureVerification = ctx.crypto->verifySignature(signedPart.data(), signedPartLen, signature.data(), signature.size(), pubKeyB);
        if(!signatureVerification) {
            errorMessage("Signed not verified", buffer);
            send(ctx.clientSocket, ctx.crypto, buffer);
            cout<<"Signed not verified"<<endl;
            return;
        }

        // M5: 2||{2||g^a mod p||<g^a mod p || n_b>PK_a}SA ->
        buffer.clear();
        ctx.crypto->keyGeneration(keyDHA);
        buffer.push_back(OP_REQUEST_TO_TALK);
        pubKeyDHLen = ctx.crypto->serializePublicKey(keyDHA, pubKeyDHBuffer.data());
        append(pubKeyDHBuffer, pubKeyDHLen, buffer);

        signature.clear();
        signature.insert(signature.end(), pubKeyDHBuffer.begin(), pubKeyDHBuffer.begin() + pubKeyDHLen);
        signature.insert(signature.end(), peerNonce.begin(), peerNonce.end());
        signedPartLen = ctx.crypto->sign(signature.data(), signature.size(), signedPart.data(), ctx.prvKeyClient);
        append(signedPart, signedPartLen, buffer);
        send(ctx.clientSocket, ctx.crypto, buffer);

        // M7: <- 2||{{2||success}AB}
        receive(ctx.clientSocket, ctx.crypto, buffer);
        if(buffer.at(0) == OP_ERROR){
            cout<<extract(buffer)<<endl;
            return;
        }
        ctx.crypto->secretDerivation(keyDHA, keyDHB, pubKeyDHBuffer.data());
        ctx.crypto->insertKey(pubKeyDHBuffer.data(), CLIENT_SECRET);
        decrypt(ctx.crypto, CLIENT_SECRET, buffer);
        cout<<extract(buffer)<<endl;
    } catch(const exception& e) {
        throw;
    }    
}