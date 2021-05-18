#include <iostream>
#include <string>
#include "symbols.h"
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>

using namespace std;

class Crypto {
    private:
        // Controllare che non restino salvate da qualche parte altrimenti non è sicura!!
        unsigned char *session_key;

    public:
        Crypto(unsigned char *sk) {
            session_key = sk;
        };

        //EVP_PKEY* readPrivateKey(string pwd);
        unsigned char* generateNonce();
        int encryptMessage(unsigned char *msg, unsigned char *ciphr_msg, unsigned char *tag);
        int decryptMessage(unsigned char *ciphr_msg, unsigned char *msg);
};