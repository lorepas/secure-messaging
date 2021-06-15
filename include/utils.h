#include <array>
#include <vector>
#include <algorithm>
#include <openssl/bio.h>
#include "socket.h"
#include "symbols.h"

void printBuffer(std::vector<unsigned char> buffer) {
    BIO_dump_fp(stdout, (const char*)buffer.data(), buffer.size());
}

template<size_t contentSize>
void append(std::array<unsigned char, contentSize> content, unsigned int contentLen, std::vector<unsigned char> &buffer) {
    unsigned char sizeArray[2];
    uint16_t size = 0;

    if (contentLen > UINT16_MAX)
        throw std::runtime_error("Content too big.");

    size = (uint16_t)contentLen;

    sizeArray[0] = size & 0xFF; //low part
    sizeArray[1] = size >> 8;   //higher part

    buffer.insert(buffer.end(), sizeArray, sizeArray + 2);
    buffer.insert(buffer.end(), content.begin(), content.begin() + contentLen);
}

template<size_t bufferSize>
int extract(std::vector<unsigned char> &content, std::array<unsigned char, bufferSize> &buffer) {
    unsigned char sizeArray[2];
    uint16_t size = 0;

    std::copy_n(content.begin(), 2, sizeArray);
    size = sizeArray[0] | uint16_t(sizeArray[1]) << 8;
    content.erase(content.begin(), content.begin() + 2);

    if(size > bufferSize)
        throw std::runtime_error("Buffer too short.");

    std::copy_n(content.begin(), size, buffer.begin());
    content.erase(content.begin(), content.begin() + size);

    return size;
}
