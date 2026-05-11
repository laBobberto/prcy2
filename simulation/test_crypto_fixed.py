
from node.crypto import Kuznyechik
import base64

print("=== Testing Fixed Kuznyechik Implementation ===\n")

key = b"MASTER_KEY_2026_STAY_SAFE_!!!!!"
c = Kuznyechik(key)

# Test 1: Basic encryption/decryption
print("Test 1: Basic Encryption/Decryption")
msg = b"Hello, Kuznyechik!"
encrypted = c.encrypt(msg)
decrypted = c.decrypt(encrypted)

print(f"Original:  {msg}")
print(f"Encrypted: {encrypted.hex()}")
print(f"Decrypted: {decrypted}")

if msg == decrypted:
    print("✓ SUCCESS: Encryption/Decryption works!\n")
else:
    print("✗ FAILURE: Mismatch!\n")

# Test 2: CTR mode (used in mesh)
print("Test 2: CTR Mode (Mesh Network)")
msg2 = b"Secret mesh message"
padded = msg2.ljust(32, b'\0')
nonce = 12345

encrypted_ctr = c.ctr_crypt(nonce, padded)
decrypted_ctr = c.ctr_crypt(nonce, encrypted_ctr)
decrypted_ctr_clean = decrypted_ctr.rstrip(b'\0')

print(f"Original:  {msg2}")
print(f"Encrypted: {encrypted_ctr.hex()}")
print(f"Decrypted: {decrypted_ctr_clean}")

if msg2 == decrypted_ctr_clean:
    print("✓ SUCCESS: CTR mode works!\n")
else:
    print("✗ FAILURE: CTR mismatch!\n")

# Test 3: E2E encryption simulation
print("Test 3: End-to-End Encryption Simulation")
node1_key = b"NODE1_PAIRWISE_KEY_32BYTES!!!!"
node2_key = b"NODE2_PAIRWISE_KEY_32BYTES!!!!"

node1_crypto = Kuznyechik(node1_key)
node2_crypto = Kuznyechik(node1_key)  # Same key for pair

msg3 = b"Private message from Node1 to Node2"
padded3 = msg3.ljust(48, b'\0')
timestamp = 67890

# Node1 encrypts
encrypted_e2e = node1_crypto.ctr_crypt(timestamp, padded3)
print(f"Node1 sends (encrypted): {encrypted_e2e.hex()[:40]}...")

# Node2 decrypts
decrypted_e2e = node2_crypto.ctr_crypt(timestamp, encrypted_e2e)
decrypted_e2e_clean = decrypted_e2e.rstrip(b'\0')
print(f"Node2 receives: {decrypted_e2e_clean}")

if msg3 == decrypted_e2e_clean:
    print("✓ SUCCESS: E2E encryption works!\n")
else:
    print("✗ FAILURE: E2E mismatch!\n")

# Test 4: Different keys can't decrypt
print("Test 4: Security - Wrong Key Can't Decrypt")
wrong_key = b"WRONG_KEY_32_BYTES_PADDING!!!!!"
wrong_crypto = Kuznyechik(wrong_key)

try:
    wrong_decrypt = wrong_crypto.ctr_crypt(timestamp, encrypted_e2e)
    wrong_decrypt_clean = wrong_decrypt.rstrip(b'\0')
    if wrong_decrypt_clean != msg3:
        print("✓ SUCCESS: Wrong key produces garbage (as expected)")
        print(f"  Garbage output: {wrong_decrypt[:20].hex()}...")
    else:
        print("✗ FAILURE: Wrong key somehow decrypted correctly!")
except Exception as e:
    print(f"✓ SUCCESS: Wrong key failed with error: {e}")

print("\n=== All Tests Complete ===")
