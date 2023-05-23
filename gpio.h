#ifndef GPIO_H
#define GPIO_H

typedef enum {
    PA = 0,
    PB = 32,
    PC = 64,
    PD = 96,
    PE = 128,
    PF = 160,
    PG = 192,
    PH = 224,
    PI = 256,
    PJ = 288,
    PK = 320,
    PL = 352,
    PM = 384,
    PN = 416
} gpio_port_t;

typedef enum {
    OUTPUT,
    INPUT
} gpio_mode_t;

void pinMode(int pin, gpio_mode_t mode);
void pinWrite(int pin, int val);
void pinToggle(int pin);
int  pinRead(int pin);

#endif
