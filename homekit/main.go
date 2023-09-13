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
)

const PID_HG_SWITCH = "BLEHGAA0101,BLEHGAA0102,BLEHGAA0103,BLEHGAA0104,BLEHGAA0105,BLEHGAA0106,BLEHGAA0107"
const PID_HG_CCT_LIGHT = "BLEHGAA0201"
const PID_HG_COLOR_LIGHT = "BLEHGAA0202,BLEHG010401,BLEHG010402"
const PID_HG_SMOKE_SENSOR = "BLEHG030301"
const PID_HG_MOTION_SENSOR = "BLEHG030201"
const PID_HG_DOOR_SENSOR = "BLEHG030601"

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
var g_hgCCTLights []HGCCTLight
var g_hgColorLights []HGColorLight
var g_hgSmokeSensors []HGSmokeSensor
var g_hgMotionSensors []HGMotionSensor
var g_hgDoorSensors []HGDoorSensor
var g_mqttClient mqtt.Client

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
    log.Println()
    log.Printf("Received message: topic: %s, Payload: %s\n", msg.Topic(), msg.Payload())
    var recvPackage MqttRecvPackage
    json.Unmarshal(msg.Payload(), &recvPackage)
    var payload MqttPayloadPackage
    json.Unmarshal([]byte(recvPackage.Payload), &payload)

    if recvPackage.ActionType == 50 {
        // Switch
        for _, sw := range(g_hgSwitches) {
            if sw.Id == payload.DeviceID && sw.DpId == payload.DpID {
                sw.OnOff = payload.DpValue
                log.Printf("Update switch onoff: %s.%d=%d", sw.Id, sw.DpId, sw.OnOff)
                if payload.DpValue == 0 {
                    sw.hkObj.Switch.On.SetValue(false)
                } else {
                    sw.hkObj.Switch.On.SetValue(true)
                }
            }
        }

        // CCT light
        for _, d := range(g_hgCCTLights) {
            if d.Id == payload.DeviceID {
                if payload.DpID == 20 {
                    d.OnOff = payload.DpValue
                    log.Printf("Update CCT Light onoff: %s=%d", d.Id, d.OnOff)
                    if payload.DpValue == 0 {
                        d.hkObj.Lightbulb.On.SetValue(false)
                    } else {
                        d.hkObj.Lightbulb.On.SetValue(true)
                    }
                } else if payload.DpID == 22 {
                    d.Brightness = payload.DpValue
                    log.Printf("Update CCT Light brightness: %s=%d", d.Id, d.Brightness)
                    d.hkObj.Lightbulb.Brightness.SetValue(d.Brightness / 2)
                } else if payload.DpID == 23 {
                    d.ColorTemperature = payload.DpValue
                    log.Printf("Update CCT Light colorTemperature: %s=%d", d.Id, d.ColorTemperature)
                    d.hkObj.Lightbulb.ColorTemperature.SetValue(d.ColorTemperature / 2)
                }
            }
        }

        // RGB light
        for _, d := range(g_hgColorLights) {
            if d.Id == payload.DeviceID {
                if payload.DpID == 20 {
                    d.OnOff = payload.DpValue
                    log.Printf("Update Color Light onoff: %s=%d", d.Id, d.OnOff)
                    if payload.DpValue == 0 {
                        d.hkObj.Lightbulb.On.SetValue(false)
                    } else {
                        d.hkObj.Lightbulb.On.SetValue(true)
                    }
                } else if payload.DpID == 22 {
                    d.Brightness = payload.DpValue
                    log.Printf("Update Color Light brightness: %s=%d", d.Id, d.Brightness)
                    d.hkObj.Lightbulb.Brightness.SetValue(d.Brightness / 2)
                } else if payload.DpID == 23 {
                    d.ColorTemperature = payload.DpValue
                    log.Printf("Update Color Light colorTemperature: %s=%d", d.Id, d.ColorTemperature)
                    d.hkObj.Lightbulb.ColorTemperature.SetValue(d.ColorTemperature / 2)
                } else if payload.DpID == 21 {
                    // d.ColorTemperature = payload.DpValue
                    // log.Printf("Update Color Light colorTemperature: %s=%d", d.Id, d.ColorTemperature)
                    // d.hkObj.Lightbulb.ColorTemperature.SetValue(d.ColorTemperature / 2)
                } else if payload.DpID == 24 {

                }
            }
        }
    } else if recvPackage.ActionType == 52 {
        // Smoke sensor
        for _, d := range(g_hgSmokeSensors) {
            if d.Id == payload.DeviceID {
                if payload.DpID == 1 {
                    d.Detected = payload.DpValue
                    log.Printf("Update smoke sensor: %s=%d", d.Id, d.Detected)
                    d.hkObj.SmokeSensor.SmokeDetected.SetValue(d.Detected)
                }
            }
        }
    } else if recvPackage.ActionType == 56 {
        // Motion sensor
        for _, d := range(g_hgMotionSensors) {
            if d.Id == payload.DeviceID {
                if payload.DpID == 101 {
                    d.Detected = payload.DpValue
                    log.Printf("Update motion sensor: %s=%d", d.Id, d.Detected)
                    if d.Detected == 0 {
                        d.hkObj.MotionSensor.MotionDetected.SetValue(false)
                    } else {
                        d.hkObj.MotionSensor.MotionDetected.SetValue(true)
                    }
                }
            }
        }
    } else if recvPackage.ActionType == 58 {
        // Door sensor
        for _, d := range(g_hgDoorSensors) {
            if d.Id == payload.DeviceID {
                if payload.DpID == 1 {
                    d.Detected = payload.DpValue
                    log.Printf("Update door sensor: %s=%d", d.Id, d.Detected)
                    d.hkObj.ContactSensor.ContactSensorState.SetValue(d.Detected)
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
    opts.SetUsername("MqttLocalHomegy")
    opts.SetPassword("Homegysmart")
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
    token := g_mqttClient.Publish(TOPIC_CTR_DEVICE, 0, false, msg)
    token.Wait()
}

func GetShadow(shadowName string) []byte {
    url := fmt.Sprintf("https://a2376tec8bakos-ats.iot.ap-southeast-1.amazonaws.com:8443/things/%s/shadow?name=%s", appConfig.ThingId, shadowName)
    certName := "c8f9a13dc7c253251b9e250439897bc010f501edd780348ecc1c2e91add22237"
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
                items := strings.Split(deviceInfo.(string), "|")
                if len(items) == 6 {
                    pid := items[1]
                    if strings.Contains(PID_HG_SWITCH, pid) {
                        dictDps := deviceObj["dictDPs"].(map[string]interface{})
                        dictNames := deviceObj["dictName"].(map[string]interface{})
                        for dp, dpValue := range dictDps {
                            if dpInt, err := strconv.Atoi(dp); err == nil {
                                dpValueInt := GetIntValue(dpValue)
                                if dpValueInt >= 0 {
                                    dpName := dictNames[dp].(string)
                                    deviceName := deviceObj["name"].(string)
                                    d := HGSwitch{Id: k, Name: deviceName + "-" + dpName, DpId: dpInt, OnOff: dpValueInt}
                                    g_hgSwitches = append(g_hgSwitches, d)
                                }
                            }
                        }
                    } else if strings.Contains(PID_HG_CCT_LIGHT, pid) {
                        dictDps := deviceObj["dictDPs"].(map[string]interface{})
                        onoff := GetIntValue(dictDps["20"])
                        brightness := GetIntValue(dictDps["22"])
                        colorTemperature := GetIntValue(dictDps["23"])
                        deviceName := deviceObj["name"].(string)
                        d := HGCCTLight{Id: k, Name: deviceName, OnOff: onoff, Brightness: brightness, ColorTemperature: colorTemperature}
                        g_hgCCTLights = append(g_hgCCTLights, d)
                    } else if strings.Contains(PID_HG_COLOR_LIGHT, pid) {
                        dictDps := deviceObj["dictDPs"].(map[string]interface{})
                        onoff := GetIntValue(dictDps["20"])
                        brightness := GetIntValue(dictDps["22"])
                        colorTemperature := GetIntValue(dictDps["23"])
                        deviceName := deviceObj["name"].(string)
                        d := HGColorLight{Id: k, Name: deviceName, OnOff: onoff, Brightness: brightness, ColorTemperature: colorTemperature}
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
                    }
                }
            }
        }
    }
}

func Switch_OnOff_Update(v bool) {
    for _, sw := range(g_hgSwitches) {
        actualOnOff := 0
        if sw.hkObj.Switch.On.Value() {
            actualOnOff = 1
        }
        if sw.OnOff != actualOnOff {
            sw.OnOff = actualOnOff
            ControlDevice(sw.Id, sw.DpId, sw.OnOff)
            break
        }
    }
}

func CCTLight_OnOff_Update(v bool) {
    for _, d := range(g_hgCCTLights) {
        actualOnOff := 0
        if d.hkObj.Lightbulb.On.Value() {
            actualOnOff = 1
        }
        if d.OnOff != actualOnOff {
            d.OnOff = actualOnOff
            ControlDevice(d.Id, 20, d.OnOff)
            break
        }
    }
}

func CCTLight_Brightness_Update(v int) {
    for _, d := range(g_hgCCTLights) {
        actualBrightness := d.hkObj.Lightbulb.Brightness.Value()
        if d.Brightness != actualBrightness {
            d.Brightness = actualBrightness
            ControlDevice(d.Id, 22, d.Brightness * 2)
            break
        }
    }
}

func CCTLight_ColorTemperature_Update(v int) {
    for _, d := range(g_hgCCTLights) {
        actualColorTemperature := d.hkObj.Lightbulb.ColorTemperature.Value()
        if d.ColorTemperature != actualColorTemperature {
            d.ColorTemperature = actualColorTemperature
            ControlDevice(d.Id, 23, d.ColorTemperature * 2)
            break
        }
    }
}

func ColorLight_OnOff_Update(v bool) {
    for _, d := range(g_hgColorLights) {
        actualOnOff := 0
        if d.hkObj.Lightbulb.On.Value() {
            actualOnOff = 1
        }
        if d.OnOff != actualOnOff {
            d.OnOff = actualOnOff
            ControlDevice(d.Id, 20, d.OnOff)
            break
        }
    }
}

func ColorLight_Brightness_Update(v int) {
    for _, d := range(g_hgColorLights) {
        actualBrightness := d.hkObj.Lightbulb.Brightness.Value()
        if d.Brightness != actualBrightness {
            d.Brightness = actualBrightness
            ControlDevice(d.Id, 22, d.Brightness * 2)
            break
        }
    }
}

func ColorLight_ColorTemperature_Update(v int) {
    for _, d := range(g_hgColorLights) {
        actualColorTemperature := d.hkObj.Lightbulb.ColorTemperature.Value()
        if d.ColorTemperature != actualColorTemperature {
            d.ColorTemperature = actualColorTemperature
            ControlDevice(d.Id, 23, d.ColorTemperature * 2)
            break
        }
    }
}

func main() {
    var accessories []*accessory.A

    MqttInit()
    GetHomeInformation()
    GetDeviceList()

    // Print device list
    for i, d := range(g_hgSwitches) {
        log.Printf("%d: %s.%d=%d - %s\n", i + 1, d.Id, d.DpId, d.OnOff, d.Name)
    }

    // Store the data in the "./db" directory.
    fs := hap.NewFsStore("./db")

    hc := accessory.NewBridge(accessory.Info{Name: "HC Homegy",})

    // Create switchs
    for i, d := range(g_hgSwitches) {
        sw := accessory.NewSwitch(accessory.Info{Name: d.Name,})
        g_hgSwitches[i].hkObj = sw
        sw.Switch.On.OnValueRemoteUpdate(Switch_OnOff_Update)
        accessories = append(accessories, sw.A)
    }

    // Create CCT Lights
    for i, d := range(g_hgCCTLights) {
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
        accessories = append(accessories, sensor.A)
    }

    // Create CCT light
    // lightbulb := accessory.NewLightbulb(accessory.Info{Name: "Đèn 1"})

    // devices := []*accessory.A {switchAs, lightbulb.A}

    // lamp1.Switch.On.OnValueRemoteUpdate(func(on bool) {
    //     if on == true {
    //         log.Println("Lamp 1 is on")
    //     } else {
    //         log.Println("Lamp 1 is off")
    //     }
    // })

    // lightbulb.Lightbulb.ColorTemperature.OnValueRemoteUpdate(func(value int) {
    //     log.Println("Đèn 1 đổi màu: " + strconv.Itoa(value))
    // })

    // go func() {
    //     for {
    //         value := lightbulb.Lightbulb.ColorTemperature.Value()
    //         log.Println(value)
    //         time.Sleep(1 * time.Second)
    //     }
    // }()

    // Create the hap server.
    server, err := hap.NewServer(fs, hc.A, accessories...)
    if err != nil {
        // stop if an error happens
        log.Panic(err)
    }
    server.Pin = "20081958"

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