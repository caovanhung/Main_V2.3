package main

import (
    mqtt "github.com/eclipse/paho.mqtt.golang"
    "github.com/brutella/hap"
    "github.com/brutella/hap/accessory"

    "io/ioutil"
    "context"
    "log"
    "os"
    "os/signal"
    "syscall"
    "strconv"
    "encoding/json"
    "fmt"
    "os/exec"
    "reflect"
    "strings"
    // "time"
    "math"
)

const PID_HG_SWITCH = "BLEHGAA0101,BLEHGAA0102,BLEHGAA0103,BLEHGAA0104"
const PID_HG_DOOR_SWITCH = "BLEHGAA0105,BLEHGAA0106,BLEHGAA0107"
const PID_HG_CCT_LIGHT = "BLEHGAA0201"
const PID_HG_COLOR_LIGHT = "BLEHGAA0202,BLEHG010401,BLEHG010402"
const PID_HG_SMOKE_SENSOR = "BLEHG030301,BLEHGAA0406"
const PID_HG_MOTION_SENSOR = "BLEHG030201,BLEHGAA0401"
const PID_HG_DOOR_SENSOR = "BLEHG030601,BLEHGAA0404"
const PID_HG_TV = "BLEHGAA0302"
const PID_HG_AC = "BLEHGAA0304"
const PID_HG_FAN = "BLEHGAA0303"

const TOPIC_CTR_DEVICE = "APPLICATION_SERVICES/Mosq/Control"
const TOPIC_RESP_DEVICE = "APPLICATION_SERVICES/AWS/#"

type AppConfig struct {
    ThingId string
    HomeId string
    IsMaster string
    HcAddr string
}


type AccountInfo struct {
    State struct {
        Reported struct {
            PageIndex0 int `json:"pageIndex0"`
            PageIndex3 int `json:"pageIndex3"`
            PageIndex2 int `json:"pageIndex2"`
        } `json:"reported"`
    } `json:"state"`
}

type HGSwitch struct {
    Id    string
    DpId  int
    OnOff int
    Name  string
    hkObj *accessory.Switch
}

type HGDoorSwitch struct {
    Id    string
    DpId  int
    CurrentState int
    TargetState  int
    Name  string
    hkObj *accessory.GarageDoorOpener
}

type HGCCTLight struct {
    Id    string
    OnOff int
    Brightness int
    ColorTemperature int
    Name  string
    hkObj *accessory.Lightbulb
}

type HGColorLight struct {
    Id    string
    OnOff int
    Brightness int
    ColorTemperature int
    Saturation float64
    Hue float64
    Name  string
    hkObj *accessory.ColoredLightbulb
}

type HGSmokeSensor struct {
    Id    string
    Detected int
    Name  string
    hkObj *accessory.SmokeSensor
}

type HGMotionSensor struct {
    Id    string
    Detected int
    Name  string
    hkObj *accessory.MotionSensor
}

type HGDoorSensor struct {
    Id    string
    Detected int
    Name  string
    hkObj *accessory.ContactSensor
}

type HGCooler struct {
    Id    string
    Active int
    CurrentTemperature float64
    Name  string
    hkObj *accessory.Cooler
}

type HGTV struct {
    Id    string
    Active int
    Name  string
    hkObj *accessory.Television
}

type HGFan struct {
    Id    string
    OnOff int
    Name  string
    hkObj *accessory.Fan
}

type MqttRecvPackage struct {
    NameService string `json:"NameService"`
    ActionType  int    `json:"ActionType"`
    TimeCreat   int64  `json:"TimeCreat"`
    PageIndex   int    `json:"pageIndex"`
    Payload     string `json:"Payload"`
}


type MqttPayloadPackage struct {
    DeviceID string `json:"deviceId"`
    DpID     int    `json:"dpId"`
    DpValue  int    `json:"dpValue"`
}

var appConfig AppConfig
var g_hgSwitches []HGSwitch
var g_hgDoorSwitches []HGDoorSwitch
var g_hgCCTLights []HGCCTLight
var g_hgColorLights []HGColorLight
var g_hgSmokeSensors []HGSmokeSensor
var g_hgMotionSensors []HGMotionSensor
var g_hgDoorSensors []HGDoorSensor
var g_hgTVs []HGTV
var g_hgFans []HGFan
var g_hgCoolers []HGCooler
var g_mqttClient mqtt.Client

func CreatePassword(thingId string) string {
    len := len([]rune(thingId))
    if len >= 8 {
        return thingId[len - 8:]
    }
    if (len == 7) {
        return fmt.Sprintf("0%s", thingId)
    }
    if (len == 6) {
        return fmt.Sprintf("00%s", thingId)
    }
    return fmt.Sprintf("000%s", thingId)
}

func GetHomeInformation() bool {
    // Get home information
    configContent, err := ioutil.ReadFile("app.json")
    if err == nil {
        json.Unmarshal(configContent, &appConfig)
        log.Println("homeId: " + appConfig.HomeId)
        return true
    } else {
        log.Println(err)
    }

    return false
}

var Mqtt_OnReceivedMessage mqtt.MessageHandler = func(client mqtt.Client, msg mqtt.Message) {

    var recvPackage MqttRecvPackage
    json.Unmarshal(msg.Payload(), &recvPackage)
    var payload MqttPayloadPackage
    json.Unmarshal([]byte(recvPackage.Payload), &payload)

    if recvPackage.ActionType == 50 || recvPackage.ActionType == 52 || recvPackage.ActionType == 56 || recvPackage.ActionType == 58 {
        log.Println()
        log.Printf("Received message: topic: %s, Payload: %s\n", msg.Topic(), msg.Payload())
    } else {
        return
    }

    if recvPackage.ActionType == 50 {
        // TV
        for i, sw := range(g_hgTVs) {
            if sw.Id == payload.DeviceID && payload.DpID == 101 {
                g_hgTVs[i].Active = payload.DpValue
                log.Printf("Update TV active: %s=%d", sw.Id, payload.DpValue)
                sw.hkObj.Television.Active.SetValue(payload.DpValue)
            }
        }

        // Fan
        for i, sw := range(g_hgFans) {
            if sw.Id == payload.DeviceID && payload.DpID == 101 {
                g_hgFans[i].OnOff = payload.DpValue
                log.Printf("Update Fan OnOff: %s=%d", sw.Id, payload.DpValue)
                sw.hkObj.Fan.On.SetValue(GetBoolValue(payload.DpValue))
            }
        }

        // Cooler
        for i, sw := range(g_hgCoolers) {
            if sw.Id == payload.DeviceID {
                if payload.DpID == 103 {
                    if payload.DpValue == 0 {
                        g_hgCoolers[i].Active = 0
                        log.Printf("Update Cooler OnOff: %s=%d", sw.Id, 0)
                        sw.hkObj.Cooler.Active.SetValue(0)
                    } else {
                        g_hgCoolers[i].Active = 1
                        log.Printf("Update Cooler OnOff: %s=%d", sw.Id, 1)
                        sw.hkObj.Cooler.Active.SetValue(1)

                        g_hgCoolers[i].CurrentTemperature = float64(payload.DpValue)
                        log.Printf("Update Cooler current temperature: %s=%d", sw.Id, payload.DpValue)
                        sw.hkObj.Cooler.CurrentTemperature.SetValue(float64(payload.DpValue))
                    }
                }
            }
        }

        // Switch
        for i, sw := range(g_hgSwitches) {
            if sw.Id == payload.DeviceID && sw.DpId == payload.DpID {
                g_hgSwitches[i].OnOff = payload.DpValue
                log.Printf("Update switch onoff: %s.%d=%d", sw.Id, sw.DpId, payload.DpValue)
                sw.hkObj.Switch.On.SetValue(GetBoolValue(payload.DpValue))
            }
        }

        // Curtain, door switch
        for i, sw := range(g_hgDoorSwitches) {
            if sw.Id == payload.DeviceID && sw.DpId == payload.DpID {
                if payload.DpValue == 0 {
                    g_hgDoorSwitches[i].CurrentState = 0    // Open
                } else if payload.DpValue == 2 {
                    g_hgDoorSwitches[i].CurrentState = 1    // Close
                } else {
                    g_hgDoorSwitches[i].CurrentState = 4    // Stop
                }

                log.Printf("Update door switch state: %s.%d=%d", sw.Id, sw.DpId, payload.DpValue)
                sw.hkObj.GarageDoorOpener.CurrentDoorState.SetValue(g_hgDoorSwitches[i].CurrentState)
            }
        }

        // CCT light
        for i, d := range(g_hgCCTLights) {
            if d.Id == payload.DeviceID {
                if payload.DpID == 20 {
                    g_hgCCTLights[i].OnOff = payload.DpValue
                    log.Printf("Update CCT Light onoff: %s=%d", d.Id, payload.DpValue)
                    d.hkObj.Lightbulb.On.SetValue(GetBoolValue(payload.DpValue))
                } else if payload.DpID == 22 {
                    g_hgCCTLights[i].Brightness = int(math.Round(float64(payload.DpValue) / 10))
                    log.Printf("Update CCT Light brightness: %s=%d", d.Id, payload.DpValue)
                    d.hkObj.Lightbulb.Brightness.SetValue(g_hgCCTLights[i].Brightness)
                } else if payload.DpID == 23 {
                    g_hgCCTLights[i].ColorTemperature = 500 - payload.DpValue / 2
                    log.Printf("Update CCT Light colorTemperature: %s=%d", d.Id, payload.DpValue)
                    d.hkObj.Lightbulb.ColorTemperature.SetValue(g_hgCCTLights[i].ColorTemperature)
                }
            }
        }

        // RGB light
        for i, d := range(g_hgColorLights) {
            if d.Id == payload.DeviceID {
                if payload.DpID == 20 {
                    g_hgColorLights[i].OnOff = payload.DpValue
                    log.Printf("Update Color Light onoff: %s=%d", d.Id, g_hgColorLights[i].OnOff)
                    d.hkObj.Lightbulb.On.SetValue(GetBoolValue(payload.DpValue))
                } else if payload.DpID == 22 {
                    g_hgColorLights[i].Brightness = payload.DpValue / 10
                    log.Printf("Update Color Light brightness: %s=%d", d.Id, payload.DpValue)
                    d.hkObj.Lightbulb.Brightness.SetValue(g_hgColorLights[i].Brightness)
                } else if payload.DpID == 23 {
                    g_hgColorLights[i].ColorTemperature = 500 - payload.DpValue / 2
                    log.Printf("Update Color Light colorTemperature: %s=%d", d.Id, payload.DpValue)
                    d.hkObj.Lightbulb.ColorTemperature.SetValue(g_hgColorLights[i].ColorTemperature)
                } else if payload.DpID == 21 {
                    // d.ColorTemperature = payload.DpValue
                    // log.Printf("Update Color Light colorTemperature: %s=%d", d.Id, d.ColorTemperature)
                    // d.hkObj.Lightbulb.ColorTemperature.SetValue(d.ColorTemperature / 2)
                } else if payload.DpID == 24 {

                }
            }
        }

        // Door sensor
        for i, d := range(g_hgDoorSensors) {
            if d.Id == payload.DeviceID {
                if payload.DpID == 1 {
                    g_hgDoorSensors[i].Detected = payload.DpValue
                    log.Printf("Update door sensor: %s=%d", d.Id, payload.DpValue)
                    d.hkObj.ContactSensor.ContactSensorState.SetValue(payload.DpValue)
                }
            }
        }

        // Smoke sensor
        for i, d := range(g_hgSmokeSensors) {
            if d.Id == payload.DeviceID {
                if payload.DpID == 1 {
                    g_hgSmokeSensors[i].Detected = payload.DpValue
                    log.Printf("Update smoke sensor: %s=%d", d.Id, payload.DpValue)
                    d.hkObj.SmokeSensor.SmokeDetected.SetValue(payload.DpValue)
                }
            }
        }

        // Motion sensor
        for i, d := range(g_hgMotionSensors) {
            if d.Id == payload.DeviceID {
                if payload.DpID == 101 {
                    g_hgMotionSensors[i].Detected = payload.DpValue
                    log.Printf("Update motion sensor: %s=%d", d.Id, payload.DpValue)
                    d.hkObj.MotionSensor.MotionDetected.SetValue(GetBoolValue(payload.DpValue))
                }
            }
        }
    } else if recvPackage.ActionType == 52 {
        // Smoke sensor
        for i, d := range(g_hgSmokeSensors) {
            if d.Id == payload.DeviceID {
                if payload.DpID == 1 {
                    g_hgSmokeSensors[i].Detected = payload.DpValue
                    log.Printf("Update smoke sensor: %s=%d", d.Id, payload.DpValue)
                    d.hkObj.SmokeSensor.SmokeDetected.SetValue(payload.DpValue)
                }
            }
        }
    } else if recvPackage.ActionType == 56 {
        // Motion sensor
        for i, d := range(g_hgMotionSensors) {
            if d.Id == payload.DeviceID {
                if payload.DpID == 101 {
                    g_hgMotionSensors[i].Detected = payload.DpValue
                    log.Printf("Update motion sensor: %s=%d", d.Id, payload.DpValue)
                    d.hkObj.MotionSensor.MotionDetected.SetValue(GetBoolValue(payload.DpValue))
                }
            }
        }
    } else if recvPackage.ActionType == 58 {
        // Door sensor
        for i, d := range(g_hgDoorSensors) {
            if d.Id == payload.DeviceID {
                if payload.DpID == 1 {
                    g_hgDoorSensors[i].Detected = payload.DpValue
                    log.Printf("Update door sensor: %s=%d", d.Id, payload.DpValue)
                    d.hkObj.ContactSensor.ContactSensorState.SetValue(payload.DpValue)
                }
            }
        }
    }


}

var Mqtt_OnConnectHandler mqtt.OnConnectHandler = func(client mqtt.Client) {
    log.Println("Mqtt is connected")
    token := g_mqttClient.Subscribe(TOPIC_RESP_DEVICE, 0, nil)
    token.Wait()
    log.Printf("Subscribed to topic: %s\n", TOPIC_RESP_DEVICE)
}

var Mqtt_OnConnectLostHandler mqtt.ConnectionLostHandler = func(client mqtt.Client, err error) {
    log.Printf("Mqtt connection lost: %v", err)
}

func MqttInit() {
    opts := mqtt.NewClientOptions()
    opts.AddBroker(fmt.Sprintf("tcp://localhost:1883"))
    opts.SetClientID("kjhfpslkjwkmcs")
    opts.SetUsername("homegyinternal")
    opts.SetPassword("sgSk@ui41DA09#Lab%1")
    opts.SetDefaultPublishHandler(Mqtt_OnReceivedMessage)
    opts.OnConnect = Mqtt_OnConnectHandler
    opts.OnConnectionLost = Mqtt_OnConnectLostHandler
    g_mqttClient = mqtt.NewClient(opts)
    if token := g_mqttClient.Connect(); token.Wait() && token.Error() != nil {
        log.Println("Cannot connect to mqtt: ", token.Error())
    }
    log.Println("Mqtt init done")
}

func ControlDevice(deviceId string, dpId int, onoff int) {
    msgFormat := `{"state":{"reported":{"type":4, "sender":2, "senderId":"homekit", "%s":{"dictDPs":{"%d":%d}}}}}`
    msg := fmt.Sprintf(msgFormat, deviceId, dpId, onoff)
    log.Printf("Sent to AWS: %s\n", msg)
    token := g_mqttClient.Publish(TOPIC_CTR_DEVICE, 0, false, msg)
    token.Wait()
}

func ControlDoorSwitch(deviceId string, dpId int, state int) {
    msgFormat := `{"state":{"reported":{"type":4, "sender":2, "senderId":"homekit", "%s":{"dictDPs":{"%d":%d}}}}}`
    if state == 1 {
        state = 2
    } else if state == 4 {
        state = 1
    }
    msg := fmt.Sprintf(msgFormat, deviceId, dpId, state)
    log.Printf("Sent to AWS: %s\n", msg)
    token := g_mqttClient.Publish(TOPIC_CTR_DEVICE, 0, false, msg)
    token.Wait()
}

func ControlLightCCT(deviceId string, brightness int, colorTemperature int) {
    msgFormat := `{"state":{"reported":{"type":4, "sender":2, "senderId":"homekit", "%s":{"dictDPs":{"22":%d, "23":%d}}}}}`
    msg := fmt.Sprintf(msgFormat, deviceId, brightness * 10, (500 - colorTemperature) * 2)
    log.Printf("Sent to AWS: %s\n", msg)
    token := g_mqttClient.Publish(TOPIC_CTR_DEVICE, 0, false, msg)
    token.Wait()
}

func ControlWifiDevice(deviceId string, dpId int, onoff int) {
    msgFormat := `{"state":{"reported":{"type":4, "sender":2, "senderId":"homekit", "%s":{"dictDPs":{"%d":%d}}}}}`
    msg := fmt.Sprintf(msgFormat, deviceId, dpId, onoff)
    log.Printf("Sent to AWS: %s\n", msg)
    token := g_mqttClient.Publish(TOPIC_CTR_DEVICE, 0, false, msg)
    token.Wait()
}

func GetShadow(shadowName string) []byte {
    url := fmt.Sprintf("https://a1i465rylwjuwn-ats.iot.ap-southeast-1.amazonaws.com:8443/things/%s/shadow?name=%s", appConfig.ThingId, shadowName)
    certName := "912ec97a119071a5b50180d37857fa97408aefeba4d7206d5135f2182fcb1d0a"
    // request := fmt.Sprintf("--tlsv1.2 --cacert /usr/bin/AmazonRootCA1.pem --cert /usr/bin/%s-certificate.pem.crt --key /usr/bin/%s-private.pem.key  %s", certName, certName, url)
    certParam := fmt.Sprintf("/usr/bin/%s-certificate.pem.crt", certName)
    keyParam := fmt.Sprintf("/usr/bin/%s-private.pem.key", certName)
    log.Println("Requesting: " + url)
    out, err := exec.Command("curl", url, "--tlsv1.2", "--cacert", "/usr/bin/AmazonRootCA1.pem", "--cert", certParam, "--key", keyParam).Output()
    if err != nil {
        log.Fatal(err)
    }
    log.Printf("Response: %s\n", out)
    return out
}

func GetDeviceList() {
    accountInfoStr := GetShadow("accountInfo")
    var accountInfo AccountInfo
    json.Unmarshal(accountInfoStr, &accountInfo)
    devicePages := accountInfo.State.Reported.PageIndex0
    // groupPages := accountInfo.State.Reported.PageIndex3
    for p := 1; p <= devicePages; p++ {
        deviceInfoStr := GetShadow("d_" + strconv.Itoa(p))
        var devicesObj map[string]interface{}
        var state map[string]interface{}
        var reported map[string]interface{}
        json.Unmarshal(deviceInfoStr, &devicesObj)
        state = devicesObj["state"].(map[string]interface{})
        reported = state["reported"].(map[string]interface{})
        for k, v := range reported {
            if (reflect.ValueOf(v).Kind() == reflect.Map) {
                deviceObj := v.(map[string]interface{})
                deviceInfo := deviceObj["devices"]
                if _, ok := deviceInfo.(string); ok {
                    items := strings.Split(deviceInfo.(string), "|")
                    if len(items) == 6 {
                        pid := items[1]
                        if strings.Contains(PID_HG_SWITCH, pid) {
                            dictDps := deviceObj["dictDPs"].(map[string]interface{})
                            var dictNames map[string]interface{} = nil
                            if deviceObj["dictName"] != nil {
                                dictNames = deviceObj["dictName"].(map[string]interface{})
                            }
                            for dp, dpValue := range dictDps {
                                if dpInt, err := strconv.Atoi(dp); err == nil {
                                    dpValueInt := GetIntValue(dpValue)
                                    if dpValueInt >= 0 {
                                        dpName := dp
                                        if dictNames != nil {
                                            dpName = dictNames[dp].(string)
                                        }
                                        deviceName := deviceObj["name"].(string)
                                        d := HGSwitch{Id: k, Name: deviceName + "-" + dpName, DpId: dpInt, OnOff: dpValueInt}
                                        g_hgSwitches = append(g_hgSwitches, d)
                                    }
                                }
                            }
                        } else if strings.Contains(PID_HG_DOOR_SWITCH, pid) {
                            dictDps := deviceObj["dictDPs"].(map[string]interface{})
                            var dictNames map[string]interface{} = nil
                            if deviceObj["dictName"] != nil {
                                dictNames = deviceObj["dictName"].(map[string]interface{})
                            }
                            for dp, dpValue := range dictDps {
                                if dpInt, err := strconv.Atoi(dp); err == nil {
                                    dpValueInt := GetIntValue(dpValue)
                                    if (dpValueInt == 2) {
                                        dpValueInt = 1   // Closed
                                    } else if (dpValueInt == 1) {
                                        dpValueInt = 4   // Stopped
                                    }
                                    if dpValueInt >= 0 {
                                        dpName := dp
                                        if dictNames != nil {
                                            dpName = dictNames[dp].(string)
                                        }
                                        deviceName := deviceObj["name"].(string)
                                        d := HGDoorSwitch{Id: k, Name: deviceName + "-" + dpName, DpId: dpInt, CurrentState: dpValueInt, TargetState: 1}
                                        g_hgDoorSwitches = append(g_hgDoorSwitches, d)
                                    }
                                }
                            }
                        } else if strings.Contains(PID_HG_CCT_LIGHT, pid) {
                            dictDps := deviceObj["dictDPs"].(map[string]interface{})
                            onoff := GetIntValue(dictDps["20"])
                            brightness := GetIntValue(dictDps["22"])
                            colorTemperature := GetIntValue(dictDps["23"])
                            deviceName := deviceObj["name"].(string)
                            d := HGCCTLight{Id: k, Name: deviceName, OnOff: onoff, Brightness: brightness / 10, ColorTemperature: 500 - colorTemperature / 2}
                            g_hgCCTLights = append(g_hgCCTLights, d)
                        } else if strings.Contains(PID_HG_COLOR_LIGHT, pid) {
                            dictDps := deviceObj["dictDPs"].(map[string]interface{})
                            onoff := GetIntValue(dictDps["20"])
                            brightness := GetIntValue(dictDps["22"])
                            colorTemperature := GetIntValue(dictDps["23"])
                            deviceName := deviceObj["name"].(string)
                            d := HGColorLight{Id: k, Name: deviceName, OnOff: onoff, Brightness: brightness / 10, ColorTemperature: 500 - colorTemperature / 2}
                            g_hgColorLights = append(g_hgColorLights, d)
                        } else if strings.Contains(PID_HG_SMOKE_SENSOR, pid) {
                            dictDps := deviceObj["dictDPs"].(map[string]interface{})
                            detected := GetIntValue(dictDps["1"])
                            deviceName := deviceObj["name"].(string)
                            d := HGSmokeSensor{Id: k, Name: deviceName, Detected: detected}
                            g_hgSmokeSensors = append(g_hgSmokeSensors, d)
                        } else if strings.Contains(PID_HG_MOTION_SENSOR, pid) {
                            dictDps := deviceObj["dictDPs"].(map[string]interface{})
                            detected := GetIntValue(dictDps["101"])
                            deviceName := deviceObj["name"].(string)
                            d := HGMotionSensor{Id: k, Name: deviceName, Detected: detected}
                            g_hgMotionSensors = append(g_hgMotionSensors, d)
                        } else if strings.Contains(PID_HG_DOOR_SENSOR, pid) {
                            dictDps := deviceObj["dictDPs"].(map[string]interface{})
                            detected := GetIntValue(dictDps["1"])
                            deviceName := deviceObj["name"].(string)
                            d := HGDoorSensor{Id: k, Name: deviceName, Detected: detected}
                            g_hgDoorSensors = append(g_hgDoorSensors, d)
                        } else if strings.Contains(PID_HG_TV, pid) {
                            dictDps := deviceObj["dictDPs"].(map[string]interface{})
                            active, err := strconv.Atoi(dictDps["101"].(string))
                            if err == nil {
                                deviceName := deviceObj["name"].(string)
                                d := HGTV{Id: k, Name: deviceName, Active: active}
                                g_hgTVs = append(g_hgTVs, d)
                            } else {
                                log.Printf("Parse tv is error: %+v\n", err)
                            }
                        } else if strings.Contains(PID_HG_FAN, pid) {
                            dictDps := deviceObj["dictDPs"].(map[string]interface{})
                            onoff, err := strconv.Atoi(dictDps["101"].(string))
                            if err == nil {
                                deviceName := deviceObj["name"].(string)
                                d := HGFan{Id: k, Name: deviceName, OnOff: onoff}
                                g_hgFans = append(g_hgFans, d)
                            } else {
                                log.Printf("Parse fan is error: %+v\n", err)
                            }
                        } else if strings.Contains(PID_HG_AC, pid) {
                            dictDps := deviceObj["dictDPs"].(map[string]interface{})
                            active := GetIntValue(dictDps["103"])
                            currentTemperature := GetIntValue(dictDps["103"])
                            deviceName := deviceObj["name"].(string)
                            d := HGCooler{Id: k, Name: deviceName, Active: active, CurrentTemperature: float64(currentTemperature)}
                            g_hgCoolers = append(g_hgCoolers, d)
                        }
                    }
                }
            }
        }
    }
}

func SyncDeviceState() {
    for _, sw := range(g_hgSwitches) {
        sw.hkObj.Switch.On.SetValue(GetBoolValue(sw.OnOff))
    }

    for _, sw := range(g_hgDoorSwitches) {
        // log.Println(sw)
        sw.hkObj.GarageDoorOpener.CurrentDoorState.SetValue(2)
    }

    // CCT light
    for _, d := range(g_hgCCTLights) {
        d.hkObj.Lightbulb.On.SetValue(GetBoolValue(d.OnOff))
        d.hkObj.Lightbulb.Brightness.SetValue(d.Brightness)
        d.hkObj.Lightbulb.ColorTemperature.SetValue(d.ColorTemperature)
    }

    // RGB light
    for _, d := range(g_hgColorLights) {
        d.hkObj.Lightbulb.On.SetValue(GetBoolValue(d.OnOff))
        d.hkObj.Lightbulb.Brightness.SetValue(d.Brightness)
        d.hkObj.Lightbulb.ColorTemperature.SetValue(d.ColorTemperature)
    }

    // Smoke sensor
    log.Printf("Number of smoke sensors: %d\n", len(g_hgSmokeSensors))
    for i, d := range(g_hgSmokeSensors) {
        log.Printf("    %d: %s=%s\n", i + 1, d.Id, d.Name)
        d.hkObj.SmokeSensor.SmokeDetected.SetValue(d.Detected)
    }

    // Motion sensor
    log.Printf("Number of motion sensors: %d\n", len(g_hgMotionSensors))
    for i, d := range(g_hgMotionSensors) {
        log.Printf("    %d: %s=%s\n", i + 1, d.Id, d.Name)
        d.hkObj.MotionSensor.MotionDetected.SetValue(GetBoolValue(d.Detected))
    }

    // Door sensor
    log.Printf("Number of door sensors: %d\n", len(g_hgDoorSensors))
    for i, d := range(g_hgDoorSensors) {
        log.Printf("    %d: %s=%s\n", i + 1, d.Id, d.Name)
        d.hkObj.ContactSensor.ContactSensorState.SetValue(d.Detected)
    }

    // TV
    log.Printf("Number of TVs: %d\n", len(g_hgTVs))
    for i, d := range(g_hgTVs) {
        log.Printf("    %d: %s=%s\n", i + 1, d.Id, d.Name)
        d.hkObj.Television.Active.SetValue(d.Active)
    }

    // Fan
    log.Printf("Number of Fans: %d\n", len(g_hgFans))
    for i, d := range(g_hgFans) {
        log.Printf("    %d: %s=%s\n", i + 1, d.Id, d.Name)
        d.hkObj.Fan.On.SetValue(GetBoolValue(d.OnOff))
    }

    // Air conditioner
    log.Printf("Number of air conditioners: %d\n", len(g_hgCoolers))
    for i, d := range(g_hgCoolers) {
        log.Printf("    %d: %s=%s\n", i + 1, d.Id, d.Name)
        d.hkObj.Cooler.Active.SetValue(d.Active)
        d.hkObj.Cooler.CurrentTemperature.SetValue(d.CurrentTemperature)
    }
}

func Switch_OnOff_Update(v bool) {
    log.Printf("Switch_OnOff_Update: %t\n", v)
    for i, sw := range(g_hgSwitches) {
        actualOnOff := 0
        if sw.hkObj.Switch.On.Value() {
            actualOnOff = 1
        }
        if sw.OnOff != actualOnOff {
            ControlDevice(sw.Id, sw.DpId, actualOnOff)
            g_hgSwitches[i].OnOff = actualOnOff
            break
        }
    }
}

func Door_State_Update(v int) {
    log.Printf("Door_State_Update: %d\n", v)
    // log.Println(g_hgDoorSwitches)
    for i, sw := range(g_hgDoorSwitches) {
        actualState := 0
        actualState = sw.hkObj.GarageDoorOpener.TargetDoorState.Value()
        log.Println(sw.hkObj.GarageDoorOpener.CurrentDoorState.Value(), sw.hkObj.GarageDoorOpener.TargetDoorState.Value())
        if sw.TargetState != actualState {
            ControlDoorSwitch(sw.Id, sw.DpId, actualState)
            g_hgDoorSwitches[i].TargetState = actualState
            g_hgDoorSwitches[i].CurrentState = actualState
            // log.Println(g_hgDoorSwitches)
            // break
        }
    }
}

func CCTLight_OnOff_Update(v bool) {
    log.Printf("CCTLight_OnOff_Update: %t\n", v)
    // log.Println(g_hgCCTLights)
    for i, d := range(g_hgCCTLights) {
        actualOnOff := 0
        if d.hkObj.Lightbulb.On.Value() {
            actualOnOff = 1
        }

        if d.OnOff != actualOnOff {
            ControlDevice(d.Id, 20, actualOnOff)
            g_hgCCTLights[i].OnOff = actualOnOff
            // log.Println(g_hgCCTLights)
            break
        }
    }
}

func CCTLight_Brightness_Update(v int) {
    log.Printf("CCTLight_Brightness_Update: %d\n", v)
    for i, d := range(g_hgCCTLights) {
        actualBrightness := d.hkObj.Lightbulb.Brightness.Value()
        if d.Brightness != actualBrightness {
            log.Printf("Test: %+v\n", d)
            ControlLightCCT(d.Id, actualBrightness, d.ColorTemperature)
            g_hgCCTLights[i].Brightness = actualBrightness
            break
        }
    }
}

func CCTLight_ColorTemperature_Update(v int) {
    log.Printf("CCTLight_ColorTemperature_Update: %d\n", v)
    for i, d := range(g_hgCCTLights) {
        actualColorTemperature := d.hkObj.Lightbulb.ColorTemperature.Value()
        if d.ColorTemperature != actualColorTemperature {
            ControlLightCCT(d.Id, d.Brightness, actualColorTemperature)
            g_hgCCTLights[i].ColorTemperature = actualColorTemperature
            break
        }
    }
}

func ColorLight_OnOff_Update(v bool) {
    log.Printf("ColorLight_OnOff_Update: %t\n", v)
    for i, d := range(g_hgColorLights) {
        actualOnOff := 0
        if d.hkObj.Lightbulb.On.Value() {
            actualOnOff = 1
        }
        if d.OnOff != actualOnOff {
            ControlDevice(d.Id, 20, actualOnOff)
            g_hgColorLights[i].OnOff = actualOnOff
            break
        }
    }
}

func ColorLight_Brightness_Update(v int) {
    log.Printf("ColorLight_Brightness_Update: %d\n", v)
    for i, d := range(g_hgColorLights) {
        actualBrightness := d.hkObj.Lightbulb.Brightness.Value()
        if d.Brightness != actualBrightness {
            ControlLightCCT(d.Id, actualBrightness, d.ColorTemperature)
            g_hgColorLights[i].Brightness = actualBrightness
            break
        }
    }
}

func ColorLight_ColorTemperature_Update(v int) {
    log.Printf("ColorLight_ColorTemperature_Update: %d\n", v)
    for i, d := range(g_hgColorLights) {
        actualColorTemperature := d.hkObj.Lightbulb.ColorTemperature.Value()
        if d.ColorTemperature != actualColorTemperature {
            ControlLightCCT(d.Id, d.Brightness, actualColorTemperature)
            g_hgColorLights[i].ColorTemperature = actualColorTemperature
            break
        }
    }
}

func TV_Active_Update(v int) {
    log.Printf("TV_Active_Update: %t\n", v)
    for i, d := range(g_hgTVs) {
        actualActive := d.hkObj.Television.Active.Value()
        if d.Active != actualActive {
            ControlDevice(d.Id, 20, actualActive)
            g_hgTVs[i].Active = actualActive
            break
        }
    }
}

func Fan_OnOff_Update(v bool) {
    log.Printf("Fan_OnOff_Update: %t\n", v)
    for i, d := range(g_hgFans) {
        actualOnOff := 0
        if d.hkObj.Fan.On.Value() {
            actualOnOff = 1
        }
        if d.OnOff != actualOnOff {
            ControlDevice(d.Id, 20, actualOnOff)
            g_hgFans[i].OnOff = actualOnOff
            break
        }
    }
}

func Cooler_Active_Update(v int) {
    log.Printf("Cooler_Active_Update: %t\n", v)
    for i, d := range(g_hgCoolers) {
        actualActive := d.hkObj.Cooler.Active.Value()
        if d.Active != actualActive {
            ControlDevice(d.Id, 20, actualActive)
            g_hgCoolers[i].Active = actualActive
            break
        }
    }
}

func main() {
    var accessories []*accessory.A

    MqttInit()
    GetHomeInformation()
    GetDeviceList()

    // // Print device list
    // log.Printf("Number of sensors: %d\n", len(g_hgDoorSensors))
    // for i, d := range(g_hgDoorSensors) {
    //     log.Printf("%d: %s=%s\n", i + 1, d.Id, d.Name)
    // }

    // Store the data in the "./db" directory.
    fs := hap.NewFsStore("./db")

    hc := accessory.NewBridge(accessory.Info{Name: fmt.Sprintf("HC %s", appConfig.ThingId),})

    // Create switchs
    for i, d := range(g_hgSwitches) {
        sw := accessory.NewSwitch(accessory.Info{Name: d.Name,})
        g_hgSwitches[i].hkObj = sw
        sw.Switch.On.OnValueRemoteUpdate(Switch_OnOff_Update)
        accessories = append(accessories, sw.A)
    }

    // Create door switchs
    for i, d := range(g_hgDoorSwitches) {
        sw := accessory.NewGarageDoorOpener(accessory.Info{Name: d.Name,})
        g_hgDoorSwitches[i].hkObj = sw
        sw.GarageDoorOpener.TargetDoorState.OnValueRemoteUpdate(Door_State_Update)
        accessories = append(accessories, sw.A)
    }
    // log.Println(g_hgCCTLights)
    // Create CCT Lights
    for i, d := range(g_hgCCTLights) {
        // log.Println(d)
        light := accessory.NewLightbulb(accessory.Info{Name: d.Name,})
        g_hgCCTLights[i].hkObj = light
        light.Lightbulb.On.OnValueRemoteUpdate(CCTLight_OnOff_Update)
        light.Lightbulb.ColorTemperature.OnValueRemoteUpdate(CCTLight_ColorTemperature_Update)
        light.Lightbulb.Brightness.OnValueRemoteUpdate(CCTLight_Brightness_Update)
        accessories = append(accessories, light.A)
    }

    // Create Color Lights
    for i, d := range(g_hgColorLights) {
        light := accessory.NewColoredLightbulb(accessory.Info{Name: d.Name,})
        g_hgColorLights[i].hkObj = light
        light.Lightbulb.On.OnValueRemoteUpdate(ColorLight_OnOff_Update)
        light.Lightbulb.ColorTemperature.OnValueRemoteUpdate(ColorLight_ColorTemperature_Update)
        light.Lightbulb.Brightness.OnValueRemoteUpdate(ColorLight_Brightness_Update)
        accessories = append(accessories, light.A)
    }

    // Create Smoke sensors
    for i, d := range(g_hgSmokeSensors) {
        sensor := accessory.NewSmokeSensor(accessory.Info{Name: d.Name,})
        g_hgSmokeSensors[i].hkObj = sensor
        accessories = append(accessories, sensor.A)
    }

    // Create Motion sensors
    for i, d := range(g_hgMotionSensors) {
        sensor := accessory.NewMotionSensor(accessory.Info{Name: d.Name,})
        g_hgMotionSensors[i].hkObj = sensor
        accessories = append(accessories, sensor.A)
    }

    // Create Door sensors
    for i, d := range(g_hgDoorSensors) {
        sensor := accessory.NewContactSensor(accessory.Info{Name: d.Name,})
        g_hgDoorSensors[i].hkObj = sensor
        log.Println(d.Name)
        accessories = append(accessories, sensor.A)
    }

    // Create TVs
    for i, d := range(g_hgTVs) {
        tv := accessory.NewTelevision(accessory.Info{Name: d.Name,})
        tv.Television.Active.OnValueRemoteUpdate(TV_Active_Update)
        g_hgTVs[i].hkObj = tv
        accessories = append(accessories, tv.A)
    }

    // Create Fans
    for i, d := range(g_hgFans) {
        fan := accessory.NewFan(accessory.Info{Name: d.Name,})
        fan.Fan.On.OnValueRemoteUpdate(Fan_OnOff_Update)
        g_hgFans[i].hkObj = fan
        accessories = append(accessories, fan.A)
    }

    // Create Air conditioners
    for i, d := range(g_hgCoolers) {
        cooler := accessory.NewCooler(accessory.Info{Name: d.Name,})
        cooler.Cooler.Active.OnValueRemoteUpdate(Cooler_Active_Update)
        g_hgCoolers[i].hkObj = cooler
        accessories = append(accessories, cooler.A)
    }


    // Create the hap server.
    server, err := hap.NewServer(fs, hc.A, accessories...)
    if err != nil {
        // stop if an error happens
        log.Panic(err)
    }
    server.Pin = CreatePassword(appConfig.ThingId)

    // Setup a listener for interrupts and SIGTERM signals
    // to stop the server.
    c := make(chan os.Signal)
    signal.Notify(c, os.Interrupt)
    signal.Notify(c, syscall.SIGTERM)

    ctx, cancel := context.WithCancel(context.Background())
    go func() {
        <-c
        // Stop delivering signals.
        signal.Stop(c)
        // Cancel the context to stop the server.
        cancel()
    }()

    // Run the server.
    log.Println("Homekit server is starting")
    SyncDeviceState()
    server.ListenAndServe(ctx)
}


func GetIntValue(value any) int {
    valueInt := -1
    if tmp, ok := value.(int); ok {
        valueInt = tmp
    } else if valueFloat, ok := value.(float64); ok {
        valueInt = int(valueFloat)
    } else if dpValueBool, ok := value.(bool); ok {
        if dpValueBool == true {
            valueInt = 1
        } else {
            valueInt = 0
        }
    }
    return valueInt
}

func GetBoolValue(value int) bool {
    if value == 1 {
        return true
    } else {
        return false
    }
}