
# Kuznyechik (GOST R 34.12-2015) - Improved Implementation

PI = [
    0xFC, 0xEE, 0xDD, 0x11, 0xCF, 0x6E, 0x31, 0x16, 0xFB, 0xC4, 0xFA, 0xDA, 0x23, 0xC5, 0x04, 0x4D,
    0xE9, 0x77, 0xF0, 0xDB, 0x93, 0x2E, 0x99, 0xBA, 0x17, 0x36, 0xF1, 0xBB, 0x14, 0xCD, 0x5F, 0xC1,
    0xF9, 0x18, 0x65, 0x5A, 0xE2, 0x5C, 0xEF, 0x21, 0x81, 0x1C, 0x3C, 0x42, 0x8B, 0x01, 0x8E, 0xAE,
    0x05, 0x84, 0x19, 0xCE, 0xAD, 0x08, 0xB1, 0x12, 0x10, 0x3B, 0xA4, 0x70, 0xF0, 0x08, 0xC2, 0x13,
    0x17, 0x8F, 0x64, 0x9B, 0x0E, 0x73, 0x39, 0x76, 0x0B, 0xD4, 0xB4, 0x51, 0x13, 0x02, 0x04, 0x18,
    0x61, 0x35, 0x12, 0x19, 0xE5, 0x6A, 0xA2, 0x17, 0x0A, 0x23, 0x4D, 0x52, 0xAD, 0x1B, 0x32, 0xA8,
    0x12, 0x2E, 0x2D, 0x18, 0x35, 0x19, 0x30, 0x44, 0x39, 0xC5, 0x38, 0x02, 0xA3, 0x05, 0x16, 0x1C,
    0xA9, 0x17, 0x04, 0x19, 0x32, 0x16, 0x1E, 0x12, 0x13, 0xA9, 0x1A, 0x0E, 0x12, 0x1D, 0x16, 0x1C,
    0x31, 0x4E, 0x52, 0x04, 0x69, 0x65, 0xB5, 0x0E, 0x7A, 0x33, 0x08, 0x2B, 0x0B, 0xD4, 0xB4, 0x51,
    0x17, 0x0A, 0x23, 0x4D, 0x52, 0xAD, 0x1B, 0x32, 0xA8, 0x12, 0x2E, 0x2D, 0x18, 0x35, 0x19, 0x30,
    0xE2, 0x5C, 0xEF, 0x21, 0x81, 0x1C, 0x3C, 0x42, 0x8B, 0x01, 0x8E, 0xAE, 0x05, 0x84, 0x19, 0xCE,
    0xAD, 0x08, 0xB1, 0x12, 0x10, 0x3B, 0xA4, 0x70, 0xF0, 0x08, 0xC2, 0x13, 0x17, 0x8F, 0x64, 0x9B,
    0xFC, 0xEE, 0xDD, 0x11, 0xCF, 0x6E, 0x31, 0x16, 0xFB, 0xC4, 0xFA, 0xDA, 0x23, 0xC5, 0x04, 0x4D,
    0xE9, 0x77, 0xF0, 0xDB, 0x93, 0x2E, 0x99, 0xBA, 0x17, 0x36, 0xF1, 0xBB, 0x14, 0xCD, 0x5F, 0xC1,
    0xF9, 0x18, 0x65, 0x5A, 0xE2, 0x5C, 0xEF, 0x21, 0x81, 0x1C, 0x3C, 0x42, 0x8B, 0x01, 0x8E, 0xAE,
    0x05, 0x84, 0x19, 0xCE, 0xAD, 0x08, 0xB1, 0x12, 0x10, 0x3B, 0xA4, 0x70, 0xF0, 0x08, 0xC2, 0x13
]

INV_PI = [0] * 256
for i in range(256):
    INV_PI[PI[i]] = i

L_COEFFS = [148, 32, 133, 16, 194, 192, 1, 251, 1, 192, 194, 16, 133, 32, 148, 1]

def galois_mul(a, b):
    p = 0
    for _ in range(8):
        if b & 1:
            p ^= a
        hi = a & 0x80
        a = (a << 1) & 0xFF
        if hi:
            a ^= 0xC3
        b >>= 1
    return p

from cryptography.hazmat.primitives.asymmetric import x25519, ed25519
from cryptography.hazmat.primitives import serialization

class X25519Auth:
    @staticmethod
    def generate_key_pair():
        priv = x25519.X25519PrivateKey.generate()
        pub = priv.public_key()
        return priv, pub

    @staticmethod
    def get_shared_secret(priv, peer_pub_bytes):
        peer_pub = x25519.X25519PublicKey.from_public_bytes(peer_pub_bytes)
        return priv.exchange(peer_pub)

class Ed25519Auth:
    @staticmethod
    def generate_key_pair():
        priv = ed25519.Ed25519PrivateKey.generate()
        pub = priv.public_key()
        return priv, pub

    @staticmethod
    def sign(priv, message):
        return priv.sign(message)

    @staticmethod
    def verify(pub_bytes, message, signature):
        pub = ed25519.Ed25519PublicKey.from_public_bytes(pub_bytes)
        try:
            pub.verify(signature, message)
            return True
        except:
            return False

class Kuznyechik:
    def __init__(self, key):
        self.key = key if len(key) == 32 else key.ljust(32, b'\0')
        self.round_keys = self._expand_key(self.key)

    def _expand_key(self, key):
        keys = [bytearray(key[:16]), bytearray(key[16:32])]
        for i in range(2, 10):
            prev = bytearray(keys[i-1])
            for j in range(16):
                prev[j] = PI[prev[j]] ^ i
            prev = bytearray(self._l(bytes(prev)))
            keys.append(prev)
        return keys

    def _x(self, a, b):
        return bytes([x ^ y for x, y in zip(a, b)])

    def _s(self, block):
        return bytes([PI[b] for b in block])

    def _inv_s(self, block):
        return bytes([INV_PI[b] for b in block])

    def _l(self, block):
        block = bytearray(block)
        for _ in range(16):
            res = 0
            for i in range(16):
                res ^= galois_mul(block[i], L_COEFFS[i])
            # Match C: memmove(block + 1, block, 15); block[0] = res;
            for j in range(15, 0, -1):
                block[j] = block[j-1]
            block[0] = res
        return bytes(block)

    def _inv_l(self, block):
        block = bytearray(block)
        for _ in range(16):
            # Match C inv_l_func
            saved = block[0]
            for j in range(15):
                block[j] = block[j+1]
            block[15] = saved
            
            res = 0
            for i in range(16):
                res ^= galois_mul(block[i], L_COEFFS[i])
            block[15] = res
        return bytes(block)


    def encrypt_block(self, block):
        res = bytearray(block)
        for i in range(9):
            res = bytearray(self._x(res, self.round_keys[i]))
            res = bytearray(self._s(res))
            res = bytearray(self._l(res))
        res = bytearray(self._x(res, self.round_keys[9]))
        return bytes(res)

    def decrypt_block(self, block):
        res = bytearray(block)
        res = bytearray(self._x(res, self.round_keys[9]))
        for i in range(8, -1, -1):
            res = bytearray(self._inv_l(res))
            res = bytearray(self._inv_s(res))
            res = bytearray(self._x(res, self.round_keys[i]))
        return bytes(res)

    def encrypt(self, data):
        pad_len = 16 - (len(data) % 16)
        data += bytes([pad_len] * pad_len)

        res = b""
        for i in range(0, len(data), 16):
            res += self.encrypt_block(data[i:i+16])
        return res

    def decrypt(self, data):
        res = b""
        for i in range(0, len(data), 16):
            res += self.decrypt_block(data[i:i+16])

        if len(res) > 0:
            pad_len = res[-1]
            if pad_len > 0 and pad_len <= 16:
                return res[:-pad_len]
        return res

    def ctr_crypt(self, nonce, data):
        result = bytearray(data)
        for i in range(0, len(data), 16):
            counter_block = bytearray(16)
            counter_block[0:4] = nonce.to_bytes(4, 'little')
            counter_block[4:8] = (i // 16).to_bytes(4, 'little')
            keystream = self.encrypt_block(bytes(counter_block))
            for j in range(min(16, len(data) - i)):
                result[i + j] ^= keystream[j]
        return bytes(result)

    def _cmac_shift_left(self, data):
        res = bytearray(16)
        carry = 0
        for i in range(15, -1, -1):
            next_carry = 1 if (data[i] & 0x80) else 0
            res[i] = ((data[i] << 1) & 0xFF) | carry
            carry = next_carry
        return bytes(res)

    def _cmac_generate_subkeys(self):
        l = self.encrypt_block(b'\x00' * 16)
        if l[0] & 0x80:
            k1 = bytearray(self._cmac_shift_left(l))
            k1[15] ^= 0x87
            k1 = bytes(k1)
        else:
            k1 = self._cmac_shift_left(l)

        if k1[0] & 0x80:
            k2 = bytearray(self._cmac_shift_left(k1))
            k2[15] ^= 0x87
            k2 = bytes(k2)
        else:
            k2 = self._cmac_shift_left(k1)
        return k1, k2

    def mac(self, data):
        k1, k2 = self._cmac_generate_subkeys()
        n = (len(data) + 15) // 16
        if n == 0:
            n = 1
        
        state = bytearray(16)
        for i in range(n - 1):
            block = data[i*16:(i+1)*16]
            for j in range(16):
                state[j] ^= block[j]
            state = bytearray(self.encrypt_block(bytes(state)))

        last_block = bytearray(16)
        last_len = len(data) - (n - 1) * 16
        last_block[0:last_len] = data[(n - 1) * 16:]
        
        if last_len == 16:
            for j in range(16):
                state[j] ^= last_block[j] ^ k1[j]
        else:
            last_block[last_len] = 0x80
            for j in range(16):
                state[j] ^= last_block[j] ^ k2[j]
        
        return self.encrypt_block(bytes(state))

