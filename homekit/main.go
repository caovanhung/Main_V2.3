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

const PID_HG_SWITCH = "BLEHGAA0101,BLEHGAA0102,BLEHGAA0103,BLEHGAA0104"
const PID_HG_CCT_LIGHT = "BLEHGAA0201"
const PID_HG_COLOR_LIGHT = "BLEHGAA0202,BLEHG010401,BLEHG010402"
const TOPIC_CTR_DEVICE = "APPLICATION_SERVICES/Mosq/Control"
const TOPIC_RESP_DEVICE = "APPLICATION_SERVICES/AWS/50"

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
    for _, sw := range(g_hgSwitches) {
        if sw.Id == payload.DeviceID && sw.DpId == payload.DpID {
            sw.OnOff = payload.DpValue
            log.Printf("ControlDevice: %s.%d=%d", sw.Id, sw.DpId, sw.OnOff)
            if payload.DpValue == 0 {
                sw.hkObj.Switch.On.SetValue(false)
            } else {
                sw.hkObj.Switch.On.SetValue(true)
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
    msgFormat := `"state":{"reported":{"type":4, "sender":2, "senderId":"homekit", "%s":{"dictDPs":{"%d":%d}}}}`
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
                                dpValueInt := -1
                                if dpValueFloat, ok := dpValue.(float64); ok {
                                    dpValueInt = int(dpValueFloat)
                                } else if dpValueBool, ok := dpValue.(bool); ok {
                                    if dpValueBool == true {
                                        dpValueInt = 1
                                    } else {
                                        dpValueInt = 0
                                    }
                                }

                                if dpValueInt >= 0 {
                                    dpName := dictNames[dp].(string)
                                    deviceName := deviceObj["name"].(string)
                                    d := HGSwitch{Id: k, Name: deviceName + "_" + dpName, DpId: dpInt, OnOff: dpValueInt}
                                    g_hgSwitches = append(g_hgSwitches, d)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

func OnSwitchRemoteUpdate(v bool) {
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
        sw.Switch.On.OnValueRemoteUpdate(OnSwitchRemoteUpdate)
        accessories = append(accessories, sw.A)
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