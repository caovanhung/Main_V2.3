package accessory

import (
    "github.com/brutella/hap/service"
)

type Cooler struct {
    *A
    Cooler *service.Cooler
}

type SmokeSensor struct {
    *A
    SmokeSensor *service.SmokeSensor
}

type MotionSensor struct {
    *A
    MotionSensor *service.MotionSensor
}

type ContactSensor struct {
    *A
    ContactSensor *service.ContactSensor
}

// NewCooler returns a cooler accessory.
func NewCooler(info Info) *Cooler {
    a := Cooler{}
    a.A = New(info, TypeAirConditioner)

    a.Cooler = service.NewCooler()
    a.AddS(a.Cooler.S)

    return &a
}

func NewSmokeSensor(info Info) *SmokeSensor {
    a := SmokeSensor{}
    a.A = New(info, TypeSensor)

    a.SmokeSensor = service.NewSmokeSensor()
    a.AddS(a.SmokeSensor.S)

    return &a
}

func NewMotionSensor(info Info) *MotionSensor {
    a := MotionSensor{}
    a.A = New(info, TypeSensor)

    a.MotionSensor = service.NewMotionSensor()
    a.AddS(a.MotionSensor.S)

    return &a
}

func NewContactSensor(info Info) *ContactSensor {
    a := ContactSensor{}
    a.A = New(info, TypeSensor)

    a.ContactSensor = service.NewContactSensor()
    a.AddS(a.ContactSensor.S)

    return &a
}