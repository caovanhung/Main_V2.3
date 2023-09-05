import os
import sys
import json
import boto3
import array as arr
import subprocess
from boto3.session import Session
import sys


ACCESS_KEY = 'AKIA5L2ZVKTYU5XVPNGE'
SECRET_KEY = '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'
appInfoFilePath = '/home/szbaijie/hc_bin/app.json'
runningFolder = '/home/szbaijie/hc_bin'

session = Session(aws_access_key_id = ACCESS_KEY, aws_secret_access_key = SECRET_KEY)
s3 = session.resource('s3')

def uploadfile(remoteFile, localFile, homeId):
    s3.Bucket('homegylogs').upload_file(localFile, homeId + "/" + remoteFile)

if __name__ == '__main__':
    if (len(sys.argv) == 3):
        fileName = sys.argv[1]
        homeId = sys.argv[2]
        serviceName = ' '
        if 'aws' in fileName:
            serviceName = 'aws'
        elif 'core' in fileName:
            serviceName = 'core'
        elif 'ble' in fileName:
            serviceName = 'ble'
        elif 'cfg' in fileName:
            serviceName = 'cfg'
        elif 'tuya' in fileName:
            serviceName = 'tuya'

        if serviceName != ' ':
            fullPath = "/home/szbaijie/hc_bin/logs/" + serviceName + "/" + fileName
            print ('Uploading file ' + fullPath)
            uploadfile(fileName, fullPath, homeId)
        else:
            print('Service name is invalid ' + serviceName + ", homeId=" + homeId)