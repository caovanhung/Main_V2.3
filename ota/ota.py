import os  
import sys  
import json
import boto3
import array as arr
import subprocess
# import paho.mqtt.client as paho
from boto3.session import Session
import sys


ACCESS_KEY = 'AKIAVWDIAT6MK7YCDZWI'
SECRET_KEY = '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'
appInfoFilePath = '/home/szbaijie/hc_bin/app.json'
runningFolder = '/home/szbaijie/hc_bin'

g_serviceNames = {
    "HG_AWS":  "hg_aws.service",
    "HG_CORE": "hg_core.service",
    "HG_BLE":  "hg_ble.service",
    "HG_WIFI": "hg_wifi.service",
    "HG_CFG":  "hg_cfg.service",
    "homekit":  "hg_homekit.service"
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
        s3.Bucket('otahgble').download_file(fileName, folder + "/" + fileName)
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
            print("Execute: systemctl stop hg_aws")
            print("Execute: systemctl stop hg_core")
            print("Execute: systemctl stop hg_ble")
            print("Execute: systemctl stop hg_wifi")
            print("Execute: systemctl stop hg_cfg")
            print("Execute: systemctl stop hg_homekit")
            os.system("systemctl stop hg_aws")
            os.system("systemctl stop hg_core")
            os.system("systemctl stop hg_ble")
            os.system("systemctl stop hg_wifi")
            os.system("systemctl stop hg_cfg")
            os.system("systemctl stop hg_homekit")

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

        print("\nSUCCESS")
    else:
        print("FAILED")