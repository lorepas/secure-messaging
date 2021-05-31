#include <iostream>
#include <string>
#include <string.h>
#include "symbols.h"
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <openssl/bio.h>
#include <openssl/err.h>

using namespace std;
//TODO: mettere il throw runtime_exception
//TODO: mettere una funzione per stampare gli errori
class Crypto {
    private:
        // Controllare che non restino salvate da qualche parte altrimenti non è sicura!!
        unsigned char *session_key;
        unsigned char *iv;

        int generateIV();
    public:
        Crypto(unsigned char *sk) {
            session_key = new unsigned char[KEY_SIZE];
            iv = new unsigned char[IV_SIZE];

            for(int i = 0; i < KEY_SIZE; i++) {
                session_key[i] = sk[i];
            }

            for(int i = 0; i < IV_SIZE; i++) {
                iv[i] = 0;
            }
        }

        ~Crypto() {
            delete session_key;
            delete iv;
        }

        void setSessionKey(unsigned char* secret, unsigned int size);

        unsigned char* stringToChar(string str);
        string charToString(unsigned char* value);
        void generateNonce(unsigned char* nonce, unsigned int nonceLen);

        EVP_PKEY* readPrivateKey(string pwd);
        EVP_PKEY* readPublicKey(string user);
        unsigned char* getIV();

        // Authenticated encryption
        int encryptMessage(unsigned char *msg, int msg_len, unsigned char *ciphr_msg, unsigned char *tag);
        int decryptMessage(unsigned char *ciphr_msg, int ciphr_len, unsigned char *iv_src, unsigned char* tag, unsigned char *msg);
       
        // Certificates
        X509* loadCertificate();
        int sendCertificate(X509* cert, unsigned char* cert_buf);
        X509* receiveCertificate(int cert_len,unsigned char* cert_buff);
        X509_CRL* loadCRL();
        bool verifyCertificate(unsigned char* cert_buff, int cert_len);

        // Public Key handling
        int serializePublicKey(EVP_PKEY* prv_key, unsigned char* pubkey_buf);
        EVP_PKEY* deserializePublicKey(unsigned char* pubkey_buf, int pubkey_size);

        // Hash
        unsigned char* computeHash(unsigned char* msg, unsigned int msg_size);

        //Diffie-Hellmann
        EVP_PKEY* buildParameters();
        EVP_PKEY* keyGeneration(EVP_PKEY* dh_params);
        unsigned char* secretDerivation(EVP_PKEY* my_pubkey, EVP_PKEY* peer_pubkey, size_t &secretlen);
};