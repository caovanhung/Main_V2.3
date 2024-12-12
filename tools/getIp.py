import socket

def get_ip_address():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect(("8.8.8.8", 80))
    return s.getsockname()[0]

ipAddress = ""
interfaces = socket.if_nameindex()
for i in interfaces:
    if (i[1] == "ap0"):
        ipAddress = "192.168.12.1"

if (ipAddress != "192.168.12.1"):
    ipAddress = get_ip_address()

print(ipAddress, end='')
