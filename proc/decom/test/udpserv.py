#!/usr/bin/python3

import socket
import time

ip = "127.0.0.1"
port = 8080

listeningAddress = (ip, port)

datagramSocket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
#datagramSocket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
datagramSocket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
datagramSocket.bind(listeningAddress)


data = "abcd"
while(True):
    datagramSocket.sendto(data.encode(), ("127.0.0.1", 8080))
    print("sent")
    time.sleep(3)
