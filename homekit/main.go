package main

import (
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
    "time"
)

const PID_HG_SWITCH = "BLEHGAA0101,BLEHGAA0102,BLEHGAA0103,BLEHGAA0104"
const PID_HG_CCT_LIGHT = "BLEHGAA0201"
const PID_HG_COLOR_LIGHT = "BLEHGAA0202,BLEHG010401,BLEHG010402"

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

type Device struct {
    Id   string
    Name string
    Pid  string
}

var appConfig AppConfig
var g_devices []Device

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
    fmt.Printf("Response: %s\n", out)
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
                pid := strings.Split(deviceInfo.(string), "|")[1]
                // log.Println(k, pid)
                g_devices = append(g_devices, Device{Id: k, Pid: pid, Name: deviceObj["name"].(string)})
            }
        }
    }
}

func OnSwitchRemoteUpdate(v bool) {

}

func main() {
    var switchs []*accessory.Switch
    var accessories []*accessory.A

    GetHomeInformation()
    GetDeviceList()

    // Print device list
    for i, d := range(g_devices) {
        log.Printf("%d: %s - %s - %s\n", i + 1, d.Id, d.Pid, d.Name)
    }

    // Store the data in the "./db" directory.
    fs := hap.NewFsStore("./db")

    hc := accessory.NewBridge(accessory.Info{Name: "HC Homegy",})

    // Create switchs
    for _, d := range(g_devices) {
        if strings.Contains(PID_HG_SWITCH, d.Pid) {
            sw := accessory.NewSwitch(accessory.Info{Name: d.Name,})
            sw.Switch.On.OnValueRemoteUpdate(OnSwitchRemoteUpdate)
            switchs = append(switchs, sw)
            accessories = append(accessories, sw.A)
        }
    }
    
    // Create CCT light
    lightbulb := accessory.NewLightbulb(accessory.Info{Name: "Đèn 1"})

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

    go func() {
        for {
            value := lightbulb.Lightbulb.ColorTemperature.Value()
            log.Println(value)
            time.Sleep(1 * time.Second)
        }
    }()

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
    fmt.Println("Homekit server is starting")
    server.ListenAndServe(ctx)
}