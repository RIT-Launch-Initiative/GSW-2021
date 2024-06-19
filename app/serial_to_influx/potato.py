import struct
import socket
import serial

struct_format = 'fff'

udp_address = ('<broadcast>', 13000)

# Lists to store the last 10 values
values1 = []
values2 = []
values3 = []

def rolling_average(values, new_value, size=10):
    """
    Update the rolling average with the new value.
    """
    if len(values) >= size:
        values.pop(0)
    values.append(new_value)
    return sum(values) / len(values)

def parse_and_send(line, udp_socket):
    """
    Parse a line of serial input and send the parsed data via UDP.
    """
    parts = line.split(',')
    if len(parts) != 4:
        return

    try:
        index = int(parts[0])  # Index (not used in the struct)
        value1 = float(parts[1])
        value2 = float(parts[2])
        value3 = float(parts[3])
        print(f"{value1} {value2} {value3}")

        # Update rolling averages
        # avg1 = rolling_average(values1, value1)
        # avg2 = rolling_average(values2, value2)
        avg1 = 0
        avg2 = 0
        avg3 = rolling_average(values3, value3)

        packed_data = struct.pack(struct_format, avg1, avg2, avg3)
        udp_socket.sendto(packed_data, udp_address)
    except ValueError:
        print(f"Invalid line: {line}")

def read_from_serial(port, baudrate):
    """
    Read data from the serial port and process each line.
    """
    udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    
    while True:
        with serial.Serial(port, baudrate, timeout=1) as ser:
            try:
                while True:
                    line = ser.readline().decode('utf-8').strip()
                    if line:
                        parse_and_send(line, udp_socket)
            except Exception as e:
                print(f"Encountered exception: {e}. Restarting serial")

    udp_socket.close()

if __name__ == "__main__":
    serial_port = '/dev/ttyUSB0'
    baudrate = 115200
    
    read_from_serial(serial_port, baudrate)
