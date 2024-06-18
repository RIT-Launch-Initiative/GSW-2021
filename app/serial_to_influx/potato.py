import struct
import socket
import serial

struct_format = 'fff'

udp_address = ('<broadcast>', 13000)

prev1 = 0 
prev2 = 0 
prev3 = 0 

def parse_and_send(line, udp_socket):
    """
    Parse a line of serial input and send the parsed data via UDP.
    """
    global prev1 
    global prev2 
    global prev3
    parts = line.split(',')
    if len(parts) != 4:
        return
    
    try:
        index = int(parts[0])  # Index (not used in the struct)
        value1 = float(parts[1])
        value2 = float(parts[2])
        value3 = float(parts[3])
        print(str(value1) + " " + str(value2) + " " + str(value3)) 

        if prev1 == 0 and prev2 == 0 and prev3 == 0:
            prev1 = value1 
            prev2 = value2 
            prev3 = value3
            return
        else:
            
            prev1 += value1 
            prev2 += value2
            prev3 += value3

            prev1 /= 2 
            prev2 /= 2 
            prev3 /= 3
        packed_data = struct.pack(struct_format, prev1, prev2, prev3)
        udp_socket.sendto(packed_data, udp_address)
    except ValueError:
        print(f"Invalid line: {line}")

def read_from_serial(port, baudrate):
    """
    Read data from the serial port and process each line.
    """
    udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    
    with serial.Serial(port, baudrate, timeout=1) as ser:
        while True:
            line = ser.readline().decode('utf-8').strip()
            if line:
                parse_and_send(line, udp_socket)
    
    udp_socket.close()

if __name__ == "__main__":
    serial_port = '/dev/ttyUSB0'  
    baudrate = 115200
    
    read_from_serial(serial_port, baudrate)

