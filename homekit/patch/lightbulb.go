// THIS FILE IS AUTO-GENERATED
package service

import (
	"github.com/brutella/hap/characteristic"
)

const TypeLightbulb = "43"

type Lightbulb struct {
	*S

	On *characteristic.On
	ColorTemperature *characteristic.ColorTemperature
}

func NewLightbulb() *Lightbulb {
	s := Lightbulb{}
	s.S = New(TypeLightbulb)

	s.On = characteristic.NewOn()
	s.AddC(s.On.C)

	s.ColorTemperature = characteristic.NewColorTemperature()
	s.AddC(s.ColorTemperature.C)

	return &s
}