#include "app_inverter_discovery.h"
#include "app_inverter_modbus_wire.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    assert(argc == 5);
    assert(!app_inverter_discovery_modbus_supported("huawei_cloud"));
    assert(!app_inverter_discovery_modbus_supported(NULL));
    assert(app_inverter_discovery_modbus_supported(argv[1]));
    inv_mb_t bus = {.fd = -1};
    assert(inv_mb_open(&bus, "127.0.0.1", (uint16_t)atoi(argv[2])));
    app_inverter_discovery_result_t r = {.port = (uint16_t)atoi(argv[2]), .unit = (uint8_t)atoi(argv[3])};
    bool found = app_inverter_discovery_modbus(argv[1], argv[4], r.unit, bus.fd, 1600, &r);
    assert(fcntl(bus.fd, F_GETFD) == -1 && errno == EBADF);
    printf("%u\n%u\n%s\n%s\n%s\n", found, r.verified, r.model, r.serial, r.device);
}
