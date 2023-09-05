import os  
import sys  
import json
import boto3
import array as arr
import subprocess
# import paho.mqtt.client as paho
from boto3.session import Session
import sys


ACCESS_KEY = 'AKIA5L2ZVKTYU5XVPNGE'
SECRET_KEY = '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'
appInfoFilePath = '/home/szbaijie/hc_bin/app.json'
runningFolder = '/home/szbaijie/hc_bin'

g_serviceNames = {
    "HG_AWS":  "hg_aws.service",
    "HG_CORE": "hg_core.service",
    "HG_BLE":  "hg_ble.service",
    "HG_WIFI": "hg_wifi.service",
    "HG_CFG":  "hg_cfg.service"
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


def downloadfile(folder, fileName):
    # Delete old file if exist
    if (deleteFileIfExist(folder + "/" + fileName) == False):
        return False

    failed = 0
    while (True):
        s3.Bucket('otable').download_file(fileName, folder + "/" + fileName)
        # Verify file is downloaded
        if (os.path.isfile(folder + "/" + fileName) == False):
            failed += 1
            if (failed > 2):
                return False
        else:
            return True


folder = "/home/szbaijie/ota_tmp"
isExist = os.path.exists(folder)
if not isExist:
   os.makedirs(folder)

version = int(sys.argv[1])
print("New version: " + str(version))

# Download zip file
zipFile = "hc_bin_" + str(version) + ".zip"
print("Downloading: " + zipFile)
if (downloadfile(folder, zipFile)):
    # Delete old files
    for service in g_serviceNames:
        if (os.path.isfile(folder + "/" + service)):
            os.remove(folder + "/" + service)

    # Extract zip file
    print("Unziping downloaded file")
    os.system("unzip " + folder + "/" + zipFile + " -d " + folder)

    # Check new program files
    ok = True
    for service in g_serviceNames:
        if (os.path.isfile(folder + "/" + service) == False):
            print("File " + service + " is not exists in folder " + folder)
            ok = False
            break

    if (ok):
        # Update new program
        print("\nUpdating new program...")
        for service in g_serviceNames:
            # Stop program
            print("Execute: systemctl stop hg_*")
            os.system("systemctl stop hg_*")

            #Copy new program to runningFolder
            cmd = f"sudo scp {folder}/{service} {runningFolder}/{service}"
            print("Execute: " + cmd)
            os.system(cmd)

            #Chmod for new program to usr/bin
            cmd = f"sudo chmod 777 {runningFolder}/{service}"
            print("Execute: " + cmd)
            os.system(cmd)

        # Reload services
        print("\nExecute: sudo systemctl daemon-reload");
        os.system("sudo systemctl daemon-reload")

        # Reboot system
        print("\nRebooting system");
        os.system("reboot")
    else:
        print("FAILED")