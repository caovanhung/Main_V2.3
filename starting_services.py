import socket
import sys
import json
import os

def getIpAddress():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect(("8.8.8.8", 80))
    return s.getsockname()[0]

def listenCommands():
    # Create a UDP socket
    port = 5005
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    # Bind the socket to the port
    server_address = ("0.0.0.0", port)
    s.bind(server_address)

    print("Waiting for command...")
    while True:
        data, address = s.recvfrom(4096)
        cmd = data.decode('utf-8')
        print("Received command: ", cmd)
        if cmd == "GET_MASTER_IP":
            ipAddress = getIpAddress()
            s.sendto(ipAddress.encode('utf-8'), address)
            print(f"Sent to {address}: {ipAddress}")


def getMasterIp():
    port = 5005
    msg = b'GET_MASTER_IP'
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)  # UDP
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.bind(("0.0.0.0", 1000))
    while True:
        try:
            print(f'Sending GET_MASTER_IP')
            sock.sendto(msg, ("255.255.255.255", port))
            print("Waiting for response...")
            sock.settimeout(5.0)
            data, address = sock.recvfrom(4096)
            f = open("masterIP", "w")
            f.write(data.decode('utf-8'))
            print("Master HC IP: ", data.decode('utf-8'))
            os.system("systemctl restart hg_cfg")
            os.system("systemctl restart hg_ble")
            break
        except KeyboardInterrupt:
            sys.exit()
        except:
            print('Timeout. Trying again')
    sock.close()

def main():
    # Check if this HC is master or slave
    f = open("app.json", "r")
    appConfig = json.loads(f.read())
    isMaster = 0
    if "isMaster" in appConfig:
        isMaster = appConfig["isMaster"]
    print("isMaster:", isMaster)
    if (isMaster):
        os.system("systemctl restart hg_cfg")
        os.system("systemctl restart hg_ble")
        os.system("systemctl restart hg_core")
        os.system("systemctl restart hg_aws")
        os.system("systemctl restart hg_wifi")
        listenCommands()
    else:
        getMasterIp()


main()

