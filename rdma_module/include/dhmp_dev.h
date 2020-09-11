#ifndef DHMP_DEV_H
#define DHMP_DEV_H

struct dhmp_device{
	struct ib_device	*ib_device;
	struct ib_pd	*pd;
	struct ib_device_attr device_attr;
	struct list_head dev_entry;
};

void dhmp_dev_list_init(struct ib_device * ib_device,struct list_head *dev_list);
void dhmp_dev_list_destroy(struct list_head *dev_list_ptr);

#endif
