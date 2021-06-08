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

void authentication(string username, string password) {
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
        unsigned int usernameLen = username.length();

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
        delete[] buffer;
        delete[] secret;    
    } catch(const exception& e) {
        delete[] buffer;
        delete[] secret;
        throw;
    }
}

void receiveOnlineUsersList() {
    unsigned char *buffer, *plaintext;
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
        if (buffer) delete[] buffer;
        if (plaintext) delete[] plaintext;
        throw;
    }
}

void sendKey(string username, string password, EVP_PKEY *pubKey, EVP_PKEY *prvKeyDH) {
    unsigned char *buffer = NULL, *ciphertext = NULL, *encrypt_buffer = NULL;
    unsigned int bufferLen, ciphertextLen;
    unsigned int encrypt_buffer_len;
    try {
        // Generate public key
        buffer = new(nothrow) unsigned char[MAX_MESSAGE_SIZE];
        if (!buffer) throw runtime_error("Buffer not defined");
        bufferLen = crypto.serializePublicKey(prvKeyDH, buffer);

        ciphertext = new(nothrow) unsigned char[MAX_MESSAGE_SIZE];
        ciphertextLen = crypto.publicKeyEncryption(buffer, bufferLen, ciphertext, pubKey);

        // Send public key to peer
        crypto.setSessionKey(0);
        encrypt_buffer = new(nothrow) unsigned char[MAX_MESSAGE_SIZE];
        encrypt_buffer_len = crypto.encryptMessage(ciphertext, ciphertextLen, encrypt_buffer);
        // Send message to server for forwarding
        socketClient.sendMessage(socketClient.getMasterFD(), encrypt_buffer, encrypt_buffer_len);
        delete[] buffer;
        delete[] ciphertext;
        delete[] encrypt_buffer;
    }catch(const exception& e) {
        cout << "Error: " << e.what() << endl;
        if(buffer != nullptr) delete[] buffer;
        if(ciphertext != nullptr) delete[] ciphertext;
        if(encrypt_buffer != nullptr) delete[] encrypt_buffer;
        throw;
    }
}

void receiveKey(string username, string password, EVP_PKEY *prv_key_dh){
    unsigned char *ciphertext = NULL, *plaintext = NULL, *keyPeerStream = NULL, *secret = NULL;
    unsigned int ciphertextLen, plaintextLen, keyPeerStreamLen;
    EVP_PKEY *prv_key = NULL;
    EVP_PKEY *pub_key_dh = NULL;
    try{
        ciphertext = new(nothrow) unsigned char[MAX_MESSAGE_SIZE];
        // Receive peer's public key
        ciphertextLen = socketClient.receiveMessage(socketClient.getMasterFD(), ciphertext);
        crypto.setSessionKey(0);
        plaintext = new(nothrow) unsigned char[MAX_MESSAGE_SIZE];
        plaintextLen = crypto.decryptMessage(ciphertext, ciphertextLen, plaintext);

        crypto.readPrivateKey(username, password, prv_key);

        keyPeerStream = new(nothrow) unsigned char[MAX_MESSAGE_SIZE];
        keyPeerStreamLen = crypto.publicKeyDecryption(plaintext, plaintextLen, keyPeerStream, prv_key);

        crypto.deserializePublicKey(keyPeerStream, keyPeerStreamLen, pub_key_dh);

        // Secret derivation
        secret = new(nothrow) unsigned char[DIGEST_LEN];
        crypto.secretDerivation(prv_key_dh, pub_key_dh, secret);

        cout << "O' secret: " << endl;
        BIO_dump_fp(stdout, (const char*)secret, DIGEST_LEN);

        crypto.insertKey(secret, 1);
        delete[] ciphertext;
        delete[] plaintext;
        delete[] secret;
        delete[] keyPeerStream;
    }catch(const exception& e){
        cout << "receiveKey(): " << e.what() << endl;
        if(!ciphertext) delete[] ciphertext;
        if(!plaintext) delete[] plaintext;
        if(!secret) delete[] secret;
        if(!keyPeerStream) delete[] keyPeerStream;
        throw;
    }
}

void requestToTalkM1(unsigned char *nonce, string username) {
    unsigned char *message, *encryptedMessage;
    unsigned int messageLen, encryptedMessageLen, start = 0;
    try {
        message = new unsigned char[MAX_MESSAGE_SIZE];
        crypto.generateNonce(nonce);
        memcpy(message, (const char *)username.c_str(), username.length());
        start += username.length();
        memcpy(message + start, nonce, NONCE_SIZE);
        messageLen = NONCE_SIZE + username.length();

        encryptedMessage = new unsigned char[MAX_MESSAGE_SIZE];
        crypto.setSessionKey(0);
        encryptedMessageLen = crypto.encryptMessage(message, messageLen, encryptedMessage);

        memcpy(message, OP_REQUEST_TO_TALK, 1);
        start = 1;
        memcpy(message + start, encryptedMessage, encryptedMessageLen);
        messageLen = encryptedMessageLen + 1;

        socketClient.sendMessage(socketClient.getMasterFD(), message, messageLen);
        delete[] message;
        delete[] encryptedMessage;
    } catch(const exception& e) {
        cout << "Error in requestToTalkM1(): " << e.what() << endl;
        if (message) delete[] message;
        if (encryptedMessage) delete[] encryptedMessage;
        throw;
    }
}

int64_t extractNoncesCiphertextLength(unsigned char *buffer, unsigned int bufferLen) {
    int64_t value;
    memcpy(&value, buffer,sizeof(int64_t));
    return value;
}

void extractPubKeyB(EVP_PKEY *&pubKeyB, unsigned char* nonce, unsigned char* nonceB, string username, string password) {
    unsigned char *ciphertext = NULL, *plaintext = NULL, *pubKeyB_buff = NULL;
    unsigned char *nonceAreceived = NULL, *encryptedNonces = NULL, *noncesPt = NULL;
    unsigned int plaintextLen, ciphertextLen, encryptedNoncesLen;
    unsigned int pubKeyB_len;
    EVP_PKEY *prvKeyA;
    unsigned int start = 0, headerLen = 10;// "OK" + 8 bytes
    try {
        ciphertext = new unsigned char[MAX_MESSAGE_SIZE];
        plaintext = new unsigned char[MAX_MESSAGE_SIZE];
        ciphertextLen = socketClient.receiveMessage(socketClient.getMasterFD(), ciphertext);
        plaintextLen = crypto.decryptMessage(ciphertext, ciphertextLen, plaintext);
        encryptedNoncesLen = extractNoncesCiphertextLength(plaintext, plaintextLen);

        if(memcmp(plaintext+8, "OK", 2) != 0) {
            throw runtime_error("Request to talk operation not successful: OK is missing (error occurred receiving M4 of the protocol).");
        }

        encryptedNonces = new unsigned char[encryptedNoncesLen];
        start += headerLen;
        memcpy(encryptedNonces, plaintext + start, encryptedNoncesLen);

        pubKeyB_len = plaintextLen - encryptedNoncesLen - headerLen; 
        pubKeyB_buff = new unsigned char [pubKeyB_len];
        start = headerLen + encryptedNoncesLen;
        memcpy(pubKeyB_buff, plaintext + start, pubKeyB_len);
        crypto.deserializePublicKey(pubKeyB_buff, pubKeyB_len, pubKeyB);

        nonceAreceived = new unsigned char[NONCE_SIZE];
        crypto.readPrivateKey(username, password, prvKeyA);
        
        noncesPt = new unsigned char[MAX_MESSAGE_SIZE];
        crypto.publicKeyDecryption(encryptedNonces, encryptedNoncesLen, noncesPt, prvKeyA);
        
        memcpy(nonceAreceived, noncesPt, NONCE_SIZE);
        

        if (memcmp(nonce, nonceAreceived, NONCE_SIZE) != 0) {
            throw runtime_error("Request to talk operation not successful: nonce are different, the message is not fresh (error occurred receiving M4 of the protocol).");
        }
        memcpy(nonceB, noncesPt+NONCE_SIZE, NONCE_SIZE);
        delete[] ciphertext;
        delete[] plaintext;
        delete[] pubKeyB_buff;
        delete[] nonceAreceived;
        delete[] noncesPt;
    } catch(const exception& e) {
        cout << "Error in extractPubKeyB(): " << e.what() << endl;
        if (ciphertext) delete[] ciphertext;
        if (plaintext) delete[] plaintext;
        if (pubKeyB_buff) delete[] pubKeyB_buff;
        if (nonceAreceived) delete[] nonceAreceived;
        if (noncesPt) delete[] noncesPt;
        throw;
    }
}

void sendNoncesToB(EVP_PKEY* pubKeyB, unsigned char *nonce, unsigned char *nonceB) {
    unsigned char *ciphertext = NULL, *nonces = NULL, *message = NULL;
    unsigned int messageLen, ciphertextLen, start = 0;
    try {
        ciphertext = new unsigned char[MAX_MESSAGE_SIZE];
        nonces = new unsigned char[NONCE_SIZE*2];
        memcpy(nonces, nonce, NONCE_SIZE);
        memcpy(nonces + NONCE_SIZE, nonceB, NONCE_SIZE);
        ciphertextLen = crypto.publicKeyEncryption(nonces, NONCE_SIZE*2, ciphertext, pubKeyB);

        messageLen = ciphertextLen + 2;
        message = new unsigned char[ciphertextLen + 2];
        memcpy(message, "OK", 2);
        start += 2;
        memcpy(message + 2, ciphertext, ciphertextLen);

        ciphertextLen = crypto.encryptMessage(message, messageLen, ciphertext);

        socketClient.sendMessage(socketClient.getMasterFD(), ciphertext, ciphertextLen);
        delete[] ciphertext;
        delete[] nonces;
        delete[] message;
    } catch(const exception& e) {
        cout << "Error in sendNoncesToB(): " << e.what() << endl;
        if (ciphertext) delete[] ciphertext;
        if (nonces) delete[] nonces;
        if (message) delete[] message;
        throw;
    }
}

void finalizeRequestToTalk() {
    unsigned char *ciphertext = NULL, *plaintext = NULL;
    unsigned int plaintextLen, ciphertextLen;
    try {
        ciphertext = new unsigned char[MAX_MESSAGE_SIZE];
        ciphertextLen = socketClient.receiveMessage(socketClient.getMasterFD(), ciphertext);
        plaintext = new unsigned char[ciphertextLen]; // |pt| <= |ct|
        plaintextLen = crypto.decryptMessage(ciphertext, ciphertextLen, plaintext);
        if (memcmp(plaintext, "OK", 2) != 0) {
            throw runtime_error("Request to talk operation not successful: error occurred receiving M8 of the protocol");
        }
        delete[] ciphertext;
        delete[] plaintext;
    } catch(const exception& e) {
        cout << "Error in finalizeRequestToTalk(): " << e.what() << endl;
        if (ciphertext) delete[] ciphertext;
        if (plaintext) delete[] plaintext;
        throw;
    }
}

void extractPubKeyA(unsigned char *nonceA, string &peerAUsername, EVP_PKEY *&pubKeyA) {
    unsigned char *ciphertext = NULL, *plaintext = NULL, *keyBuffer = NULL, *peerAUsr = NULL;
    unsigned int ciphertextLen, plaintextLen, keyBufferLen, start = 0;
    uint64_t peerALen;

    try {
        ciphertext = new unsigned char[MAX_MESSAGE_SIZE];
        ciphertextLen = socketClient.receiveMessage(socketClient.getMasterFD(), ciphertext);

        if(memcmp(ciphertext, OP_REQUEST_TO_TALK, 1) != 0) {
            cout << "**Wrong OP" << endl;
            throw runtime_error("Request to talk operation not successful: wrong OP");
        }
        cout << "Request code verified." << endl;

        plaintext = new unsigned char[ciphertextLen-1]; //|pt| <= |ct|
        crypto.setSessionKey(0);
        plaintextLen = crypto.decryptMessage(ciphertext+1, ciphertextLen-1, plaintext); // remove the OP Code

        memcpy(&peerALen, plaintext, sizeof(uint64_t));
        start += sizeof(uint64_t);
        cout << "Peer usrname len: " << peerALen << endl;
        peerAUsr = new unsigned char[peerALen];
        memcpy(peerAUsr, plaintext + start, peerALen);
        start += peerALen;
        peerAUsername = string((const char*)peerAUsr);
        keyBufferLen = plaintextLen - NONCE_SIZE - peerALen - sizeof(uint64_t);
        keyBuffer = new unsigned char[keyBufferLen];

        memcpy(keyBuffer, plaintext + start, keyBufferLen);
        crypto.deserializePublicKey(keyBuffer, keyBufferLen, pubKeyA);
        start += keyBufferLen;
        memcpy(nonceA, plaintext+start, NONCE_SIZE);
        delete[] ciphertext;
        delete[] plaintext;
        delete[] peerAUsr;
        delete[] keyBuffer;
    } catch(const exception& e) {
        cout << "Error in extractPubKeyA(): " << e.what() << endl;
        if (ciphertext) delete[] ciphertext;
        if (plaintext) delete[] plaintext;
        if (peerAUsr) delete[] peerAUsr;
        if (keyBuffer) delete[] keyBuffer;
        throw;
    }
}

void sendNoncesToA(unsigned char *nonce, unsigned char *nonceA, EVP_PKEY *pubKeyA) {
    unsigned char *plaintext = NULL, *ciphertext = NULL, *noncesCT = NULL, *noncesPT = NULL;
    unsigned int plaintextLen, ciphertextLen, noncesCTLen, noncesPTLen = 2*NONCE_SIZE;

    try {
        crypto.generateNonce(nonce);
        noncesPT = new unsigned char[noncesPTLen];

        memcpy(noncesPT, nonceA, NONCE_SIZE);
        memcpy(noncesPT+NONCE_SIZE, nonce, NONCE_SIZE);

        noncesCT = new unsigned char[MAX_MESSAGE_SIZE];
        noncesCTLen = crypto.publicKeyEncryption(noncesPT, noncesPTLen, noncesCT, pubKeyA);

        plaintextLen = noncesCTLen + 2;
        plaintext = new unsigned char[plaintextLen];

        memcpy(plaintext, "OK", 2);
        memcpy(plaintext + 2, noncesCT, noncesCTLen);

        ciphertext = new unsigned char[MAX_MESSAGE_SIZE];
        crypto.setSessionKey(0);
        ciphertextLen = crypto.encryptMessage(plaintext, plaintextLen, ciphertext);
        socketClient.sendMessage(socketClient.getMasterFD(), ciphertext, ciphertextLen);
        delete[] plaintext;
        delete[] ciphertext;
        delete[] noncesCT;
        delete[] noncesPT;
    } catch(const exception& e) {
        cout << "Error in send nonces to A: " << e.what() << endl;
        if (plaintext) delete[] plaintext;
        if (ciphertext) delete[] ciphertext;
        if (noncesCT) delete[] noncesCT;
        if (noncesPT) delete[] noncesPT;
        throw;
    }   
}

void refuseRequestToTalk() {
    unsigned char plaintext[2];
    unsigned char *ciphertext = NULL;
    unsigned int plaintextLen = 2, ciphertextLen;
    try {
        memcpy(plaintext, "NO", 2);
        crypto.setSessionKey(0);
        ciphertext = new(nothrow) unsigned char[MAX_MESSAGE_SIZE];
        ciphertextLen = crypto.encryptMessage(plaintext, plaintextLen, ciphertext);
        socketClient.sendMessage(socketClient.getMasterFD(), ciphertext, ciphertextLen);
        delete[] ciphertext;
    } catch(const exception& e) {
        cout << e.what() << '\n';
        if(!ciphertext) delete[] ciphertext;
    }
}

void validateFreshness(unsigned char* nonce, string username, string password) {
    unsigned char *plaintext = NULL, *ciphertext = NULL, *noncesPT = NULL, *noncesCT = NULL, *nonceReceivedB = NULL;
    unsigned int plaintextLen, ciphertextLen, noncesPTLen, noncesCTLen;
    EVP_PKEY *prvKeyB;
    try {
        ciphertext = new unsigned char[MAX_MESSAGE_SIZE];
        ciphertextLen = socketClient.receiveMessage(socketClient.getMasterFD(), ciphertext);

        plaintext = new unsigned char[ciphertextLen]; //|pt| <= |ct|
        crypto.setSessionKey(0);
        plaintextLen = crypto.decryptMessage(ciphertext, ciphertextLen, plaintext);

        if(memcmp(plaintext, "OK", 2) != 0) {
            throw runtime_error("Request to talk operation not successful: wrong OP (error occurred receiving M6)");
        }

        crypto.readPrivateKey(username, password, prvKeyB);

        noncesCTLen = plaintextLen-2;
        noncesCT = new unsigned char[noncesCTLen];
        memcpy(noncesCT, plaintext+2, noncesCTLen);

        noncesPT = new unsigned char[MAX_MESSAGE_SIZE];
        noncesPTLen = crypto.publicKeyDecryption(noncesCT, noncesCTLen, noncesPT, prvKeyB);

        nonceReceivedB = new unsigned char[NONCE_SIZE];
        memcpy(nonceReceivedB, noncesPT+NONCE_SIZE, NONCE_SIZE);

        if(memcmp(nonceReceivedB, nonce, NONCE_SIZE) != 0) {
            throw runtime_error("Request to talk operation not successful: nonce are different, the message is not fresh (error occurred receiving M6).");
        }
        cout << "Freshness confirmed" << endl;

        delete[] plaintext;
        delete[] ciphertext;
        delete[] noncesPT;
        delete[] noncesCT;
        delete[] nonceReceivedB;
    } catch(const exception& e) {
        cout << "Error in validateFreshness(): " << e.what() << endl;
        if (plaintext) delete[] plaintext;
        if (ciphertext) delete[] ciphertext;
        if (noncesPT) delete[] noncesPT;
        if (noncesCT) delete[] noncesCT;
        if (nonceReceivedB) delete[] nonceReceivedB;
        throw;
    }
    
}

void sendOkMessage() {
    unsigned char *ciphertext = NULL, *plaintext = NULL;
    unsigned int ciphertextLen, plaintextLen = 2;
    try {
        plaintext = new unsigned char[plaintextLen];
        memcpy(plaintext, "OK", 2);
        crypto.setSessionKey(0);
        ciphertext = new unsigned char[MAX_MESSAGE_SIZE];
        ciphertextLen = crypto.encryptMessage(plaintext, 2, ciphertext);
        socketClient.sendMessage(socketClient.getMasterFD(), ciphertext, ciphertextLen);
        delete[] ciphertext;
        delete[] plaintext;
    } catch(const std::exception& e) {
        cout << "Error in sendOKMessage(): " << e.what() << endl;
        if (ciphertext) delete[] ciphertext;
        if (plaintext) delete[] plaintext;
        throw;
    }
}

void sendRequestToTalk(string usernameReceiver, string usernameSender, string password) {
    unsigned char *nonce = NULL, *nonceB = NULL;
    EVP_PKEY *pubKeyB = NULL;
    EVP_PKEY *prvKeyDH = NULL;
    try {
        // Send Message M1
        nonce = new(nothrow) unsigned char[NONCE_SIZE];
        requestToTalkM1(nonce, usernameReceiver);
        cout << "\nRequest to talk sent" << endl;
        // Receive Message M4:
        cout << "Waiting for confirmation..." << endl;
        nonceB = new(nothrow) unsigned char[NONCE_SIZE];
        extractPubKeyB(pubKeyB, nonce, nonceB, usernameSender, password);
        // Send Message M5:
        sendNoncesToB(pubKeyB, nonce, nonceB);
        // Receive Message M8:
        finalizeRequestToTalk();
        cout << "Request to talk accepted by " << usernameReceiver << endl;

        crypto.keyGeneration(prvKeyDH);

        cout << "Sending the key to " << usernameReceiver << endl; 
        sendKey(usernameSender, password, pubKeyB, prvKeyDH);
        cout << "Receiving the key from " << usernameReceiver << endl;
        receiveKey(usernameSender, password, prvKeyDH);
        delete[] nonce;
        delete[] nonceB;
    } catch(const exception& e) {
        cout << "Error in send request to talk: " << e.what() << endl;
        if (!nonce) delete[] nonce;
        if (!nonceB) delete[] nonceB,
        throw;
    }
}

void receiveRequestToTalk(string username, string password, string &peerAUsername) {
    unsigned char *nonce, *nonceA;
    EVP_PKEY *pubKeyA = NULL;
    EVP_PKEY *prvKeyDH = NULL;
    try {
        // Receive M2:
        cout << "Waiting for message M2..." << endl;
        nonceA = new unsigned char[NONCE_SIZE];
        extractPubKeyA(nonceA, peerAUsername, pubKeyA);

        // Send M3:
        cout << "Send Message M3" << endl;
        nonce = new unsigned char[NONCE_SIZE];
        // Accept
        string confirmation = readFromStdout("Type y to accept --> ");
        if (strcasecmp((const char *)confirmation.c_str(), "Y")) {
            cout << "The request to talk has been refused." << endl;
            refuseRequestToTalk();
            delete[] nonceA;
            delete[] nonce;
            return;
        }
        sendNoncesToA(nonce, nonceA, pubKeyA);

        // Receive M6:
        cout << "Receive M6" << endl;
        validateFreshness(nonce, username, password);
        // Send M7:
        cout << "Send Ok" << endl;
        sendOkMessage();

        crypto.keyGeneration(prvKeyDH);

        //Receive key from A
        cout << "Waiting for the key..." << endl;
        receiveKey(username, password, prvKeyDH);
        cout << "Sending the key..." << endl;
        sendKey(username, password, pubKeyA, prvKeyDH);
        delete[] nonceA;
        delete[] nonce;
    } catch(const exception& e) {
        if (nonceA) delete[] nonceA;
        if (nonce) delete[] nonce;
        throw;
    }
}

void sendMessage(string message) {
    unsigned char *ciphertext = NULL, *serverCT;
    unsigned int msgLen, ciphertextLen, serverCTLen;
    try {
        msgLen = message.length();
        unsigned char *msg = (unsigned char*)message.c_str();
        if(msgLen > MAX_MESSAGE_SIZE) {
            throw runtime_error("Message size is greater than the maximum");
        }
        crypto.setSessionKey(1);
        ciphertext = new(nothrow) unsigned char[MAX_MESSAGE_SIZE];
        if(!ciphertext){
            throw runtime_error("An error occurred during the allocation of the ciphertext for the ciphertext.");
        }
        ciphertextLen = crypto.encryptMessage(msg, msgLen, ciphertext);

        crypto.setSessionKey(0);

        serverCT = new unsigned char[MAX_MESSAGE_SIZE];
        serverCTLen = crypto.encryptMessage(ciphertext, ciphertextLen, serverCT);
        
        unsigned char buffer[serverCTLen+1];
        memcpy(buffer,OP_MESSAGE,1);
        memcpy(buffer+1,serverCT,serverCTLen);
        socketClient.sendMessage(socketClient.getMasterFD(), buffer, serverCTLen+1);
        delete[] ciphertext;
    } catch(const exception& e) {
        if(ciphertext != nullptr) delete[] ciphertext;
        throw;
    }
}

string receiveMessage(){
    unsigned char *ciphertext = NULL, *plaintext = NULL, *text;
    unsigned int cipherlen, plainlen, textLen;
    try {
        ciphertext = new(nothrow) unsigned char[MAX_MESSAGE_SIZE];
        if(!ciphertext){
            throw runtime_error("An error occurred during the allocation of the ciphertext for receiving message.");
        }
        cipherlen = socketClient.receiveMessage(socketClient.getMasterFD(), ciphertext);
        crypto.setSessionKey(0);
        plaintext = new(nothrow) unsigned char[cipherlen];
        if(!ciphertext){
            throw runtime_error("An error occurred during the allocation of the plaintext for receiving message.");
        }
        plainlen = crypto.decryptMessage(ciphertext, cipherlen, plaintext);

        crypto.setSessionKey(1);

        text = new unsigned char[MAX_MESSAGE_SIZE];
        textLen = crypto.decryptMessage(plaintext, plainlen, text);

        text[textLen] = '\0';

        return string((const char*)text);
    } catch(const exception& e) {
        if(ciphertext != nullptr) delete[] ciphertext;
        return "";
    }  
}