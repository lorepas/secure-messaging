#include "socket.h"
#include "crypto.h"
#include <cstring>
#include <termios.h>

//TODO: serve costruttore con parametri di default per fare solo dichiarazione
Crypto crypto(2);
SocketClient socketClient(SOCK_STREAM);

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
    setStdinEcho(true);
    cout << endl;

    return password;
}

string readFromStdout(string message) {
    string value;

    cout << message;
    getline(cin, value);
    while (value.length() == 0) {
        cout << "Insert at least a character." << endl;
        cout << message;
        getline(cin, value);
    }
    return value;
}

void sendHelloMessage(unsigned char* username, unsigned int usernameLen, unsigned char *nonce) {
    unsigned char *helloMessage = NULL;
    unsigned int start = 0;
    unsigned int helloMessageLen = 1 + usernameLen + NONCE_SIZE;
    try {
        helloMessage = new unsigned char[helloMessageLen];
        memcpy(helloMessage, OP_LOGIN, 1);
        start += 1;
        memcpy(helloMessage+start, username, usernameLen);
        start += usernameLen;
        memcpy(helloMessage+start, nonce, NONCE_SIZE);
        socketClient.sendMessage(socketClient.getMasterFD(), helloMessage, helloMessageLen);
    } catch(const exception& e) {
        delete[] helloMessage;
        throw;
    }
    delete[] helloMessage;
}

void extractNonce(unsigned char *clientNonce, unsigned char *serverNonce, unsigned char *receivedMessage, unsigned int messageLen) {
    unsigned char *nonceReceived = NULL;
    unsigned int start = messageLen - 2*NONCE_SIZE;
    try {
        nonceReceived = new unsigned char[NONCE_SIZE];
        memcpy(nonceReceived, receivedMessage+start, NONCE_SIZE);
        if(memcmp(nonceReceived, clientNonce, NONCE_SIZE) != 0) {
            throw runtime_error("Login Error: The freshness of the message is not confirmed");
        }
        start = messageLen - NONCE_SIZE;
        memcpy(serverNonce, receivedMessage + start, NONCE_SIZE);
    } catch (const exception& e) {
        delete[] nonceReceived;
        throw;
    }
    delete[] nonceReceived;
}

void sendPassword(unsigned char *nonce, string password, string username, X509 *cert) {
    unsigned char *buffer = NULL;
    unsigned char *pwdDigest = NULL;
    unsigned char *finalDigest = NULL;
    unsigned char *ciphertext = NULL;
    EVP_PKEY *server_pubkey = NULL;
    unsigned int start = 0;
    unsigned int ciphertextLen = 0;
    try {
        buffer = new unsigned char[NONCE_SIZE + DIGEST_LEN];
        pwdDigest = new unsigned char[DIGEST_LEN];
        crypto.computeHash((unsigned char *) password.c_str(), password.length(), pwdDigest);

        memcpy(buffer, pwdDigest, DIGEST_LEN);
        start += DIGEST_LEN;
        memcpy(buffer+start, nonce, NONCE_SIZE);

        finalDigest = new unsigned char[DIGEST_LEN];
        crypto.computeHash(buffer, DIGEST_LEN + NONCE_SIZE, finalDigest);

        crypto.getPublicKeyFromCertificate(cert,server_pubkey);
        ciphertext = new unsigned char[MAX_MESSAGE_SIZE];
        ciphertextLen = crypto.publicKeyEncryption(finalDigest, DIGEST_LEN, ciphertext, server_pubkey);
        socketClient.sendMessage(socketClient.getMasterFD(), ciphertext, ciphertextLen);
    } catch(const std::exception& e) {
        delete[] buffer;
        delete[] pwdDigest;
        delete[] finalDigest;
        delete[] ciphertext;
        throw;
    }
    delete[] buffer;
    delete[] pwdDigest;
    delete[] finalDigest;
    delete[] ciphertext;
}

void verifyServerCertificate(unsigned char *message, unsigned int messageLen, unsigned int usernameLen, X509 *&cert) {
    unsigned char *cert_buff = NULL;
    unsigned int start = usernameLen;
    unsigned int cert_len = messageLen - 2*NONCE_SIZE - usernameLen;
    try {
        cert_buff = new unsigned char[cert_len];        
        memcpy(cert_buff, message+start, cert_len);
        crypto.deserializeCertificate(cert_len, cert_buff,cert);
        if(!crypto.verifyCertificate(cert))
            throw runtime_error("Pay attention, server is not authenticated.");
        cout << "Server authenticated." << endl;
    } catch(const exception& e) {
        delete[] cert_buff;
        throw;
    }
    delete[] cert_buff;
}

void authentication() {
    unsigned char *nonceClient = NULL;
    unsigned char *nonceServer = NULL;
    unsigned char *buffer = NULL;
    unsigned char *plaintext = NULL;
    EVP_PKEY *prvkey = NULL;
    X509 *cert;
    unsigned int messageReceivedLen;
    unsigned int plainlen;
    try {
        // Get Username
        string username = readFromStdout("Insert username: ");
        unsigned int usernameLen = username.length();

        string password = readPassword();
        crypto.readPrivateKey(username,password,prvkey);

        // Generate nonce
        nonceClient = new unsigned char[NONCE_SIZE];
        crypto.generateNonce(nonceClient);

        // Build hello message
        sendHelloMessage((unsigned char *)username.c_str(), usernameLen, nonceClient);

        // Receive server hello
        buffer = new unsigned char[MAX_MESSAGE_SIZE];
        messageReceivedLen = socketClient.receiveMessage(socketClient.getMasterFD(), buffer);

        plaintext = new unsigned char[messageReceivedLen];
        plainlen = crypto.publicKeyDecryption(buffer, messageReceivedLen,plaintext,prvkey);

        // Check and extract nonce
        nonceServer = new unsigned char[NONCE_SIZE];
        extractNonce(nonceClient, nonceServer, plaintext, plainlen);

        // Verify certificate
        verifyServerCertificate(plaintext, plainlen, usernameLen, cert);

        // Send pwd
        sendPassword(nonceServer, password, username, cert);
    } catch (const exception &e) {
        delete[] nonceClient;
        delete[] buffer;
        delete[] plaintext;
        delete[] nonceServer;
        throw;
    }
    delete[] nonceClient;
    delete[] buffer;
    delete[] plaintext;
    delete[] nonceServer;
}

void sendMessage(unsigned char *header, unsigned int head_len, unsigned char *msg, unsigned int pln_len) {
    unsigned char *msg_cipher = NULL;
    unsigned char *buffer = NULL;
    unsigned int ciphr_len;
    unsigned int msg_len = 0;
    int start = 0;

    try {
        msg_cipher = new unsigned char[pln_len + TAG_SIZE + IV_SIZE];
        ciphr_len = crypto.encryptMessage(msg,pln_len,msg_cipher);

        msg_len = head_len + ciphr_len;
        if (msg_len > MAX_MESSAGE_SIZE) {
            throw runtime_error("Maximum message size exceeded");
        }

        buffer = new unsigned char[msg_len];
        memcpy(buffer, header, head_len);
        start += head_len;
        memcpy(buffer+start, msg_cipher, ciphr_len);
        start += ciphr_len;

        socketClient.sendMessage(socketClient.getMasterFD(), buffer, msg_len);
    } catch(const exception& e) {
        delete[] msg_cipher;
        delete[] buffer;
        throw;
    }
    delete[] msg_cipher;
    delete[] buffer;
}

void keyEstablishment(unsigned int key_pos) {
    unsigned char *buffer = NULL;
    unsigned char *secret = NULL;
    unsigned int key_len;
    EVP_PKEY *prv_key_a = NULL;
    EVP_PKEY *pub_key_b = NULL;

    try {
        // TODO: check where put the login request
        //socketClient.sendMessage(socketClient.getMasterFD(), OP_LOGIN, 1);
        
        // Generate public key
        crypto.keyGeneration(prv_key_a);

        // Send public key to peer
        buffer = new unsigned char[MAX_MESSAGE_SIZE];
        key_len = crypto.serializePublicKey(prv_key_a, buffer);
        socketClient.sendMessage(socketClient.getMasterFD(), buffer, key_len);

        // Receive peer's public key
        key_len = socketClient.receiveMessage(socketClient.getMasterFD(), buffer);
        crypto.deserializePublicKey(buffer, key_len, pub_key_b);

        // Secret derivation
        secret = new unsigned char[DIGEST_LEN];
        crypto.secretDerivation(prv_key_a, pub_key_b, secret);

        cout << "O' secret: " << endl;
        BIO_dump_fp(stdout, (const char*)secret, DIGEST_LEN);

        crypto.insertKey(secret, key_pos);
    } catch(const exception& e) {
        delete[] buffer;
        delete[] secret;
        throw;
    }

    delete[] buffer;
    delete[] secret;
}

void receiveOnlineUsersList() {
    unsigned char *buffer;
    unsigned char *plaintext;
    unsigned int bufferLen;
    unsigned int plaintextLen;
    try {
        buffer = new unsigned char[MAX_MESSAGE_SIZE];
        bufferLen = socketClient.receiveMessage(socketClient.getMasterFD(), buffer);
        plaintext = new unsigned char[MAX_MESSAGE_SIZE];
        plaintextLen = crypto.decryptMessage(buffer, bufferLen, plaintext);
        plaintext[plaintextLen] = '\0';
        cout << "Online users: " << endl << plaintext << endl;
    } catch(const exception& e) {
        delete[] buffer;
        delete[] plaintext;
        throw;
    }
}

void sendRequestToTalk(string username) {
    unsigned char *encryptedMessage;
    unsigned char *message;
    unsigned char *nonce;
    unsigned char *okBuffer;
    unsigned int encryptedMessageLen;
    unsigned int messageLen;
    unsigned int start = 0;
    try {

        // Send Message M1
        message = new unsigned char[MAX_MESSAGE_SIZE];
        crypto.generateNonce(nonce);
        memcpy(message, (const char *)username.c_str(), username.length());
        start += username.length();
        memcpy(message + start, nonce, NONCE_SIZE);
        
        encryptedMessage = new unsigned char[MAX_MESSAGE_SIZE];
        crypto.setSessionKey(0);
        encryptedMessageLen = crypto.encryptMessage(message, messageLen, encryptedMessage);
        memcpy(message, OP_REQUEST_TO_TALK, 1);
        start = 1;
        memcpy(message + start, encryptedMessage, encryptedMessageLen);
        messageLen = encryptedMessageLen + 1;

        socketClient.sendMessage(socketClient.getMasterFD(), message, messageLen);

        // Receive Message M4:
        encryptedMessageLen = socketClient.receiveMessage(socketClient.getMasterFD(), encryptedMessage);
        messageLen = crypto.decryptMessage(encryptedMessage, encryptedMessageLen, message);

        if(memcmp(message, "OK", 2) != 0) {
            throw runtime_error("Request to talk failed.");
        }
        
        
    } catch(const exception& e) {
        throw;
    }
    
}

void receiveRequestToTalk() {

}