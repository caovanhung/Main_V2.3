package service

import (
    "github.com/brutella/hap/characteristic"
)

type ColoredLightbulb struct {
    *S

    On         *characteristic.On
    Brightness *characteristic.Brightness
    ColorTemperature *characteristic.ColorTemperature
    Saturation *characteristic.Saturation
    Hue        *characteristic.Hue
}

func NewColoredLightbulb() *ColoredLightbulb {
    s := ColoredLightbulb{}
    s.S = New(TypeLightbulb)

    s.On = characteristic.NewOn()
    s.AddC(s.On.C)

    s.Brightness = characteristic.NewBrightness()
    s.AddC(s.Brightness.C)

    s.ColorTemperature = characteristic.NewColorTemperature()
    s.AddC(s.ColorTemperature.C)

    s.Saturation = characteristic.NewSaturation()
    s.AddC(s.Saturation.C)

    s.Hue = characteristic.NewHue()
    s.AddC(s.Hue.C)

    return &s
}
