import socket
import sys
import json
import os
import time
from pathlib import Path

homeId = ""
isMaster = 0

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
        try:
            data, address = s.recvfrom(4096)
            tmp = data.decode('utf-8')
            print("Received command: ", tmp)
            items = tmp.split(",")
            cmd = items[0]
            if cmd == "GET_MASTER_IP":
                if len(items) == 2 and items[1] == homeId:
                    ipAddress = getIpAddress()
                    s.sendto(ipAddress.encode('utf-8') , address)
                    print(f"Sent to {address}: {ipAddress}")
                else:
                    print(f"Invalid homeId. Must be {homeId}, received {items[1]}")
            elif cmd == "FIND_HC":
                ipAddress = getIpAddress()
                master = "slave"
                if isMaster == True:
                    master = "master"
                s.sendto((ipAddress + ", " + homeId + ", " + master + "\n").encode('utf-8') , address)
                print(f"Sent to {address}: {ipAddress}")
        except KeyboardInterrupt:
            sys.exit()
        except Exception as e:
            print(f'Error: {e}')

def getMasterIp():
    port = 5005
    msg = 'GET_MASTER_IP,' + homeId
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)  # UDP
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.bind(("0.0.0.0", 1000))
    failedCount = 0
    firstTimePlay = 0
    os.system("aplay /home/szbaijie/audio/slave_restarted.wav")
    while True:
        try:
            print(f'Sending ' + msg)
            sock.sendto(msg.encode('utf-8'), ("255.255.255.255", port))
            print("Waiting for response...")
            sock.settimeout(5.0)
            data, address = sock.recvfrom(4096)
            f = open("masterIP", "w")
            f.write(data.decode('utf-8'))
            print("Master HC IP: ", data.decode('utf-8'))
            os.system("aplay /home/szbaijie/audio/ready.wav")
            os.system("systemctl restart hg_ble")
            break
        except KeyboardInterrupt:
            sys.exit()
        except:
            print('Timeout. Trying again')
            failedCount = failedCount + 1
            if failedCount >= 6:
                failedCount = 0
                file_path = Path("/home/szbaijie/hc_bin/cfg")
                if not file_path.exists():
                    os.system("aplay /home/szbaijie/audio/master_not_found.wav")
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
    file_path = Path("/home/szbaijie/hc_bin/cfg")
    if file_path.exists():
        os.remove("/home/szbaijie/hc_bin/cfg");
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

    time.sleep(5)
    os.system("/home/szbaijie/hc_bin/HG_OTA 0")

    # Check if this HC is master or slave
    f = open("app.json", "r")
    fileContent = f.read()
    try:
        global homeId
        global isMaster
        appConfig = json.loads(fileContent)
        homeId = appConfig["homeId"]
        if "isMaster" in appConfig:
            isMaster = appConfig["isMaster"]
        print(f"homeId: {homeId}, isMaster: {isMaster}")
        if (isMaster):
            time.sleep(2)
            os.system("systemctl restart hg_cfg")
            time.sleep(1)
            os.system("systemctl restart hg_ble")
            time.sleep(1)
            os.system("systemctl restart hg_core")
            time.sleep(10)
            os.system("systemctl restart hg_aws")
            os.system("systemctl restart hg_wifi")
            os.system("systemctl restart hg_homekit")
            os.system("systemctl restart hg_ota")
            listenCommands()
        else:
            os.system("systemctl restart hg_cfg")
            os.system("systemctl restart hg_ota")
            os.system("systemctl restart hg_ble")
            getMasterIp()
    except Exception as e:
        print(f"Error {e}")
        os.system("systemctl restart hg_cfg")
        os.system("systemctl restart hg_ble")
        os.system("systemctl restart hg_ota")

main()

