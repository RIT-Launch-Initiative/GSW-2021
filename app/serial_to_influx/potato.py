import socket
import sys

def read_val():
    return 0

def convert_val(val):
    return val

def broadcast(val):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    
    try:
        sock.sendto(val, ("127.0.0.1", "13000")
    except Exception as e:
        print("Failed to send")
    finally:
        sock.close()

def main():
    while True:
        result = convert_val(read_val())
        broadcast(result)

if __name__ == "__main__":
    main()
