/*
 * Camera Sysfs
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/leds.h>


static struct class *camera_class = NULL;

struct class* get_camera_class(void) {
	return camera_class;
}
EXPORT_SYMBOL_GPL(get_camera_class);

int camera_class_init(void) {
	camera_class = class_create(THIS_MODULE, "camera");
	if (IS_ERR(camera_class))
		return PTR_ERR(camera_class);
	return 0;
}

void camera_class_exit(void) {
	if(camera_class)
		class_destroy(camera_class);
}

/* timelm: subsys_initcall(camera_class_init) is run by camera_main.c */
/* timelm: module_exit(camera_class_exit) is run by camera_main.c */
MODULE_LICENSE("GPL");