/*
 * Copyright (c) 2012 Corey Tabaka
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef __DEV_DRIVER_H
#define __DEV_DRIVER_H

#include <sys/types.h>
#include <list.h> // for containerof
#include <compiler.h>

struct driver;

/*
 * Contains the data pertaining to an instance of a device. More than one
 * instance may exist for a given driver type (i.e. uart0, uart1, etc..).
 */
struct device {
    const char *name;
    const struct driver *driver;

    /* node to dynamic created device list */
    struct list_node node;

    /* FDT node offset (if applicable) */
    int node_offset;

    /* instance specific config data populated at instantiation */
    const void *config;

    /* instance specific data populated by the driver at init */
    void *state;

    // TODO: add generic state, such as suspend/resume state, etc...
};

/* device class, mainly used as a unique magic pointer to validate ops */
struct device_class {
    const char *name;
};

/* standard driver ops; extensions should contain this ops structure */
struct driver_ops {
    const struct device_class *device_class;

    status_t (*init)(struct device *dev);
    status_t (*fini)(struct device *dev);

    status_t (*ioctl)(struct device *dev, int request, void *argp);

    status_t (*suspend)(struct device *dev);
    status_t (*resume)(struct device *dev);
};

/* describes a driver, one per driver type */
#define DRIVER_INIT_CORE           (1UL << 0U)
#define DRIVER_INIT_PLATFORM_EARLY (1UL << 3U)
#define DRIVER_INIT_PLATFORM       (1UL << 4U)
#define DRIVER_INIT_TARGET         (1UL << 8U)
#define DRIVER_INIT_HAL            (1UL << 12U)
#define DRIVER_INIT_HAL_VENDOR     (1UL << 14U)
#define DRIVER_INIT_APP            (1UL << 16U)

struct driver {
    const char *type;
    const struct driver_ops *ops;
    size_t prv_cfg_sz;
    unsigned initlvl;
};

/* macro-expanding concat */
#define concat(a, b) __ex_concat(a, b)
#define __ex_concat(a, b) a ## b

#define DRIVER_EXPORT(type_, ops_) \
    const struct driver concat(__driver_, type_) \
        __ALIGNED(sizeof(void *)) __SECTION(".drivers") = { \
        .type = #type_, \
        .ops = ops_, \
        .initlvl = DRIVER_INIT_PLATFORM, \
    }

#define DRIVER_EXPORT_WITH_LVL(type_, ops_, initlvl_) \
    const struct driver concat(__driver_, type_) \
        __ALIGNED(sizeof(void *)) __SECTION(".drivers") = { \
        .type = #type_, \
        .ops = ops_, \
        .initlvl = initlvl_, \
    }

#define DRIVER_EXPORT_WITH_CFG(type_, ops_, private_config_sz_) \
    const struct driver concat(__driver_, type_) \
        __ALIGNED(sizeof(void *)) __SECTION(".drivers") = { \
        .type = #type_, \
        .ops = ops_, \
        .prv_cfg_sz = private_config_sz_, \
    }

#define DRIVER_EXPORT_WITH_CFG_LVL(type_, ops_, initlvl_, private_config_sz_) \
    const struct driver concat(__driver_, type_) \
        __ALIGNED(sizeof(void *)) __SECTION(".drivers") = { \
        .type = #type_, \
        .ops = ops_, \
        .initlvl = initlvl_, \
        .prv_cfg_sz = private_config_sz_, \
    }

#define DEVICE_INSTANCE(type_, name_, config_) \
    extern struct driver concat(__driver_, type_); \
    struct device concat(__device_, concat(type_, concat(_, name_))) \
        __ALIGNED(sizeof(void *)) __SECTION(".devices") = { \
        .name = #name_, \
        .driver = &concat(__driver_, type_), \
        .config = config_, \
    }

/*
 * returns the driver specific ops pointer given the device instance, specific
 * ops type, and generic ops member name within the specific ops structure.
 */
#define device_get_driver_ops(dev, type, member) ({ \
    type *__ops = NULL; \
    if (dev && dev->driver && dev->driver->ops) \
        __ops = containerof(dev->driver->ops, type, member); \
    __ops; \
})

#define device_get_by_name(type_, name_) ({ \
    extern struct device concat(__device_, concat(type_, concat(_, name_))); \
    &concat(__device_, concat(type_, concat(_, name_))); \
})

status_t device_init_all(void);
status_t device_fini_all(void);

status_t device_init(struct device *dev);
status_t device_fini(struct device *dev);

status_t device_ioctl(struct device *dev, int request, void *argp);

status_t device_suspend(struct device *dev);
status_t device_resume(struct device *dev);

#endif

