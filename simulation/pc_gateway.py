
import socket
import sys

def run_gateway():
    print("--- UNIVERSAL MESH GATEWAY ---")
    node_idx = input("Connect to which Node (1-5)? ")
    port = 12340 + int(node_idx)
    
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(('localhost', port))
        print(f"Connected to Node {node_idx} on port {port}!")
        print("Format: <dst_id> <message>")
        print("------------------------------------------------")

        while True:
            cmd = input(f"Node{node_idx}> ")
            if not cmd: continue
            if cmd.lower() == "rotate":
                s.send("r\n".encode())
                continue
            s.send(f"s {cmd}\n".encode())
            
    except Exception as e:
        print(f"Error: {e}")
    finally:
        s.close()

if __name__ == "__main__":
    run_gateway()
