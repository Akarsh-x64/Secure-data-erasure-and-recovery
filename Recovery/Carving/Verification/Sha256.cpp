#include "Sha256.h"

#include <array>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace Recovery {
namespace Carving {
namespace Verification {
namespace {
const uint32_t k[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,
    0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
    0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,
    0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,
    0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
    0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,
    0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,
    0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
    0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};
inline uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32u - n)); }
inline uint32_t read32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
}

std::string Sha256::Compute(const std::vector<uint8_t>& data) {
    return Compute(data.data(), data.size());
}

std::string Sha256::Compute(const uint8_t* input, size_t size) {
    if (input == nullptr && size != 0) size = 0;
    std::array<uint32_t, 8> h = {
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
    };
    const uint64_t bitLength = static_cast<uint64_t>(size) * 8u;
    const size_t padded = ((size + 9u + 63u) / 64u) * 64u;
    std::vector<uint8_t> message(padded, 0);
    if (size != 0 && input != nullptr) {
        std::memcpy(message.data(), input, size);
    }
    message[size] = 0x80;
    for (unsigned i = 0; i < 8; ++i) {
        message[padded - 1u - i] = static_cast<uint8_t>(bitLength >> (i * 8u));
    }
    for (size_t offset = 0; offset < padded; offset += 64) {
        uint32_t w[64] = {};
        for (unsigned i = 0; i < 16; ++i) w[i] = read32(message.data() + offset + i * 4);
        for (unsigned i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
            const uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], t=h[7];
        for (unsigned i = 0; i < 64; ++i) {
            const uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t temp1 = t + S1 + ch + k[i] + w[i];
            const uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = S0 + maj;
            t=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
        h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=t;
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (uint32_t value : h) out << std::setw(8) << value;
    return out.str();
}
} // namespace Verification
} // namespace Carving
} // namespace Recovery
