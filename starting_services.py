import socket
import sys
import json
import os
import time

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

def removeOldLogs(folder):
    print ("Removing logs file in folder " + folder)
    now = time.time()
    cutoff = now - (14 * 86400)
    files = os.listdir(folder)
    for f in files:
        if os.path.isfile(folder + "/" + f):
            t = os.stat(folder + "/" + f)
            c = t.st_ctime

            # delete file if older than 14 days
            if c < cutoff:
                os.remove(folder + "/" + f)

def main():
    os.system("nmcli r wifi on")
    # Create logs folder if not exist
    logsPath = "/home/szbaijie/hc_bin/logs/"
    if not os.path.exists(logsPath + "aws"):
        os.makedirs(logsPath + "aws")
    if not os.path.exists(logsPath + "core"):
        os.makedirs(logsPath + "core")
    if not os.path.exists(logsPath + "ble"):
        os.makedirs(logsPath + "ble")
    if not os.path.exists(logsPath + "tuya"):
        os.makedirs(logsPath + "tuya")
    if not os.path.exists(logsPath + "cfg"):
        os.makedirs(logsPath + "cfg")
    if not os.path.exists(logsPath + "ota"):
        os.makedirs(logsPath + "ota")

    removeOldLogs(logsPath + "aws")
    removeOldLogs(logsPath + "core")
    removeOldLogs(logsPath + "ble")
    removeOldLogs(logsPath + "cfg")
    removeOldLogs(logsPath + "tuya")

    # Check if this HC is master or slave
    f = open("app.json", "r")
    fileContent = f.read()
    try:
        appConfig = json.loads(fileContent)
        isMaster = 0
        if "isMaster" in appConfig:
            isMaster = appConfig["isMaster"]
        print("isMaster:", isMaster)
        if (isMaster):
            time.sleep(2)
            os.system("systemctl restart hg_cfg")
            time.sleep(1)
            os.system("systemctl restart hg_ble")
            time.sleep(1)
            os.system("systemctl restart hg_core")
            time.sleep(15)
            os.system("systemctl restart hg_aws")
            os.system("systemctl restart hg_wifi")
            listenCommands()
        else:
            getMasterIp()
    except:
        print("HC was not configured")
        os.system("systemctl restart hg_cfg")
        os.system("systemctl restart hg_ble")

main()

