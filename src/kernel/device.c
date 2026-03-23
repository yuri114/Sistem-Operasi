#include "device.h"

static Driver *registry[DEV_MAX];

void dev_register(int id, Driver *drv) {
    if (id >= 0 && id < DEV_MAX)
        registry[id] = drv;
}

int dev_write(int id, const char *buf) {
    if (id < 0 || id >= DEV_MAX || !registry[id] || !registry[id]->write)
        return -1;
    return registry[id]->write(buf);
}

int dev_read(int id, char *buf) {
    if (id < 0 || id >= DEV_MAX || !registry[id] || !registry[id]->read)
        return -1;
    return registry[id]->read(buf);
}

int dev_ioctl(int id, int cmd, int arg) {
    if (id < 0 || id >= DEV_MAX || !registry[id] || !registry[id]->ioctl)
        return -1;
    return registry[id]->ioctl(cmd, arg);
}

void dev_init_all() {
    int i;
    for (i = 0; i < DEV_MAX; i++) {
        if (registry[i] && registry[i]->init)
            registry[i]->init();
    }
}
