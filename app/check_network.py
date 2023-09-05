import os
import socket
from datetime import datetime

def get_ip_address():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect(("8.8.8.8", 80))
    return s.getsockname()[0]

def checkInterface():
    interfaces = socket.if_nameindex()
    for i in interfaces:
        if (i[1] == "wlan0"):
            return "wlan0"

    return ""


now = datetime.now()
time = now.strftime("%d-%m-%y_%H:%M:%S")
interface = checkInterface()
ipAddr = get_ip_address()
file = open("/home/szbaijie/hc_bin/logs/cfg/checkWifiLog_" + now.strftime("%d-%m-%y") + ".txt", "a")
line = time + ": " + interface + ", " + ipAddr + "\n"
file.write(line)
file.close()
# print(line)

if (interface == "" or ("192" not in ipAddr)):
    file = open("/home/szbaijie/hc_bin/logs/cfg/checkWifiLog_" + now.strftime("%d-%m-%y") + ".txt", "a")
    line = time + ": " + interface + ", " + ipAddr + "\n"
    file.write("Restart NetworkManager service")
    file.close()
    os.system("systemctl restart NetworkManager.service")
