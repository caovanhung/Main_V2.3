#include "unistd.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "stdbool.h"

#include <sys/mman.h>  
#include <sys/types.h>  
#include <ctype.h>  
#include <sys/stat.h>  
#include <fcntl.h>  
#include "gpio.h"

void pinMode(int pin, gpio_mode_t mode) {
    FILE *p = NULL;
    char str[256];

    p = fopen("/sys/class/gpio/export", "w");
    fprintf(p, "%d", pin);
    fclose(p);
    sprintf(str, "/sys/class/gpio/gpio%d/direction", pin);
    p = fopen(str, "w");
    if (mode == OUTPUT) {
        fprintf(p, "out");
    } else {
        fprintf(p, "in");
    }
    fclose(p);
}

void pinWrite(int pin, int val) {
    FILE *p=NULL;
    char str[256];

    sprintf(str, "/sys/class/gpio/gpio%d/value", pin);
    p = fopen(str, "w");
    fprintf(p, "%d", val > 0 ? 1 : 0);
    fclose(p);
}

void pinToggle(int pin) {
    pinWrite(pin, !pinRead(pin));
}

int pinRead(int pin) {
    int port_num;
    FILE *p = NULL;
    int val;
    char str[256];

    sprintf(str, "/sys/class/gpio/gpio%d/value", pin);
    p = fopen(str, "r");
    fscanf(p, "%d", &val);
    fclose(p);

    return (int)val;
}
