import os  
import sys  
import json
import boto3
import array as arr
import subprocess
import paho.mqtt.client as paho
from boto3.session import Session

ACCESS_KEY = 'AKIA5L2ZVKTYU5XVPNGE'
SECRET_KEY = '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'
appInfoFilePath = '/home/szbaijie/hc_bin/app.json'
runningFolder = '/home/szbaijie/hc_bin'

g_serviceNames = {
    "HG_APPLICATION_SERVICES_AWS": "hg_aws.service",
    "HG_CORE_SERVICES_COREDATA":   "hg_core.service",
    "HG_DEVICE_SERVICES_BLE":      "hg_ble.service",
    "HG_DEVICE_SERVICES_WIFI":     "hg_wifi.service",
    "HG_BOARD_CONFIG":             "hg_cfg.service"
}

session = Session(aws_access_key_id = ACCESS_KEY, aws_secret_access_key = SECRET_KEY)
s3 = session.resource('s3')

def deleteFileIfExist(fileName):
    if (os.path.isfile(fileName) == False):
        return True

    failed = 0
    while (True):
        os.remove(fileName)
        if (os.path.isfile(fileName)):
            failed += 1
            if failed > 2:
                return False
        else:
            return True


def downloadfile(fileName):
    # Delete old file if exist
    if (deleteFileIfExist(fileName) == False):
        return False

    failed = 0
    while (True):
        s3.Bucket('otable').download_file(fileName, fileName)
        # Verify file is downloaded
        if (os.path.isfile(fileName) == False):
            failed += 1
            if (failed > 2):
                return False
        else:
            return True

def uploadfile(file_name):
    s3.Bucket('otable').upload_file(file_name, file_name)

def on_message(mosq, obj, msg):
    print("Receive msg: ")
    recvMsg = json.loads(msg.payload)
    print(recvMsg)
    listServiceHC = recvMsg['state']['reported']['listServiceHC']

    for program in listServiceHC:
        ver = listServiceHC[program]
        # Dowload program
        newProgram = f"{program}_{ver}"
        print("Downloading file: {0}".format(newProgram))
        if (downloadfile(newProgram) == False):
            print("Error: Cannot download the file {0}".format(newProgram))
            return

    # Update new program
    print("\nUpdating new program...")
    for program in listServiceHC:
        ver = listServiceHC[program]
        newProgram = f"{program}_{ver}"

        # Stop program
        print("Execute: systemctl stop hg_*");
        os.system("systemctl stop hg_*");

        #Copy new program to runningFolder
        cmd = f"sudo scp {newProgram} {runningFolder}/{program}"
        print("Execute: " + cmd);
        os.system(cmd)

        #Chmod for new program to usr/bin
        cmd = f"sudo chmod 777 {runningFolder}/{program}"
        print("Execute: " + cmd);
        os.system(cmd)

    # Reload services
    print("\nExecute: sudo systemctl daemon-reload");
    os.system("sudo systemctl daemon-reload")

    # Enable services
    print("\nEnabling services...")
    for program in listServiceHC:
        serviceName = g_serviceNames[program]
        cmd = f"sudo systemctl enable {serviceName}"
        print("Execute: " + cmd);
        os.system(cmd)

    # Start services
    print("\nStarting services...")
    for program in listServiceHC:
        serviceName = g_serviceNames[program]
        cmd = f"sudo systemctl start {serviceName}"
        print("Execute: " + cmd);
        os.system(cmd)

    print("\nOTA Update is done")


if __name__ == '__main__':
    client = paho.Client()
    client.on_message = on_message
    client.username_pw_set("MqttLocalHomegy", "Homegysmart")
    client.connect("localhost", 1883, 60)
    client.subscribe("MANAGER_SERVICES/ServieceManager/25/#", 0)
    while client.loop() == 0:
        pass
