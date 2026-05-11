
from crypto import Kuznyechik
import base64

key = b"secret_key_32_bytes_long_!!!!!!"
c = Kuznyechik(key)
msg = b"Hello, Kuznyechik!"
encrypted = c.encrypt(msg)
decrypted = c.decrypt(encrypted)

print(f"Original: {msg}")
print(f"Encrypted (hex): {encrypted.hex()}")
print(f"Decrypted: {decrypted}")

if msg == decrypted:
    print("SUCCESS: Encryption/Decryption works!")
else:
    print("FAILURE: Mismatch!")
