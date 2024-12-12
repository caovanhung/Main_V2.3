CC=aarch64-linux-gnu-gcc
CFLAGS= -g -O0 -funwind-tables -rdynamic -Wall -Wextra -Wconversion -Werror=int-conversion -Werror-implicit-function-declaration -Wno-discarded-qualifiers -lcrypto -lssl -ldl -lpthread -pthread -lmosquitto  -I/opt/common/ssl/include -I/opt/common/mqtt/usr/local/include -Ilib -Iapp -DBUILDTIME=\"$(shell date --iso=seconds)\"

all : HG_AWS HG_CORE HG_BLE HG_WIFI HG_CFG

HG_AWS:    app/main_aws.o lib/core_mqtt_state.o lib/openssl_posix.o lib/sockets_posix.o lib/backoff_algorithm.o lib/clock_posix.o lib/core_mqtt.o lib/core_mqtt_serializer.o lib/parson.o lib/queue.o app/aws_process.o lib/core_process_t.o app/aws_mosquitto.o lib/time_t.o lib/helper.o app/messages.o lib/cJSON.o lib/gpio.o
	$(CC) app/main_aws.o lib/core_mqtt_state.o lib/openssl_posix.o lib/sockets_posix.o lib/backoff_algorithm.o lib/clock_posix.o lib/core_mqtt.o lib/core_mqtt_serializer.o lib/parson.o lib/queue.o app/aws_process.o lib/core_process_t.o app/aws_mosquitto.o lib/time_t.o lib/helper.o app/messages.o lib/cJSON.o lib/gpio.o -lm -o HG_AWS $(CFLAGS)

HG_CORE: app/main_core.o app/database.o lib/sqlite3.o lib/time_t.o lib/queue.o lib/parson.o lib/core_process_t.o app/aws_mosquitto.o lib/helper.o app/messages.o lib/cJSON.o app/core_api.o lib/gpio.o
	$(CC)   app/main_core.o app/database.o lib/sqlite3.o lib/time_t.o lib/queue.o lib/parson.o  lib/core_process_t.o app/aws_mosquitto.o lib/helper.o app/messages.o lib/cJSON.o app/core_api.o lib/gpio.o -lm -o HG_CORE $(CFLAGS)

HG_BLE: app/main_ble.o app/ble_process.o lib/queue.o app/aws_mosquitto.o lib/time_t.o lib/uart.o lib/parson.o lib/core_process_t.o lib/helper.o app/messages.o lib/cJSON.o lib/gpio.o
	$(CC) app/main_ble.o app/ble_process.o lib/queue.o app/aws_mosquitto.o lib/time_t.o lib/uart.o lib/parson.o lib/core_process_t.o lib/helper.o app/messages.o lib/cJSON.o lib/gpio.o -lm -o HG_BLE $(CFLAGS)

HG_WIFI: app/main_wifi.o lib/time_t.o lib/queue.o lib/parson.o lib/core_process_t.o app/aws_mosquitto.o app/wifi_process.o lib/helper.o app/messages.o lib/cJSON.o lib/gpio.o
	$(CC)   app/main_wifi.o lib/time_t.o lib/queue.o lib/parson.o  lib/core_process_t.o app/aws_mosquitto.o app/wifi_process.o lib/helper.o app/messages.o lib/cJSON.o lib/gpio.o -lm -o HG_WIFI $(CFLAGS)

HG_CFG: app/main_cfg.o app/aws_mosquitto.o lib/time_t.o lib/queue.o lib/parson.o lib/helper.o lib/cJSON.o lib/gpio.o
	$(CC)   app/main_cfg.o app/aws_mosquitto.o lib/time_t.o lib/queue.o lib/parson.o lib/helper.o lib/cJSON.o lib/gpio.o -lm -o HG_CFG $(CFLAGS)

ota: ota/main_ota.o lib/core_mqtt_state.o lib/openssl_posix.o lib/sockets_posix.o lib/backoff_algorithm.o lib/clock_posix.o lib/core_mqtt.o lib/core_mqtt_serializer.o lib/parson.o lib/queue.o app/aws_process.o lib/core_process_t.o app/aws_mosquitto.o lib/time_t.o lib/helper.o app/messages.o lib/cJSON.o lib/gpio.o
	$(CC) ota/main_ota.o lib/core_mqtt_state.o lib/openssl_posix.o lib/sockets_posix.o lib/backoff_algorithm.o lib/clock_posix.o lib/core_mqtt.o lib/core_mqtt_serializer.o lib/parson.o lib/queue.o app/aws_process.o lib/core_process_t.o app/aws_mosquitto.o lib/time_t.o lib/helper.o app/messages.o lib/cJSON.o lib/gpio.o -lm -o HG_OTA $(CFLAGS)

clean:
	rm app/*.o
	rm lib/*.o
	rm HG_AWS
	rm HG_CORE
	rm HG_BLE
	rm HG_WIFI
	rm HG_CFG
