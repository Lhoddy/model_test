// #include <infiniband/verbs.h>
// #include <rdma/rdma_cma.h>
// #include <netinet/in.h>
// #include <fcntl.h>
// #include <arpa/inet.h>

#include "dhmp.h"
#include "dhmp_log.h"
#include "dhmp_hash.h"
// #include "dhmp_context.h"
#include "dhmp_dev.h"



/*
 * alloc rdma device resource
 */
static int dhmp_dev_init ( struct ib_device* ib_device, struct dhmp_device* dev_ptr )
{
	int retval=0;

	struct ib_port_attr attr;
	retval = ib_query_port(ib_device,1,&attr);
	INFO_LOG("ib_query_port %d",retval);

	memcpy(&dev_ptr->device_attr , &ib_device->attrs ,sizeof(struct ib_device_attr));
	// retval=ib_query_device ( ib_device, &dev_ptr->device_attr );
	// if ( retval<0 )
	// {
	// 	ERROR_LOG ( "ib_query_device error." );
	// 	goto out;
	// }
	
	dev_ptr->pd=ib_alloc_pd ( ib_device , 0);
	if ( !dev_ptr->pd )
	{
		ERROR_LOG ( "ib_alloc_pd error." );
		retval=-1;
		goto out;
	}

	dev_ptr->ib_device=ib_device;
	INFO_LOG("max mr %d max qp mr %d max cqe %d",
			dev_ptr->device_attr.max_mr,
			dev_ptr->device_attr.max_qp_wr,
			dev_ptr->device_attr.max_cqe);
	return retval;

out:
	dev_ptr->ib_device=NULL;
	return retval;
}

/*
 *	the function will get the rdma devices in the computer,
 * 	and init the rdma device, alloc the pd resource
 */
void dhmp_dev_list_init(struct ib_device * ib_device, struct list_head * dev_list_ptr)
{
	struct dhmp_device *dev_ptr;

	dev_ptr=(struct dhmp_device*)kernel_malloc(sizeof(struct dhmp_device));
	if(!dev_ptr)
	{
		ERROR_LOG("allocate memory error.");
		goto out;
	}
	
	dhmp_dev_init ( ib_device, dev_ptr );
	if ( !dev_ptr->ib_device )
	{
		ERROR_LOG ( "RDMA device []: allocate error.");
	}
	else
	{
		INFO_LOG ( "RDMA device []: allocate success.");
		list_add_tail(&dev_ptr->dev_entry, dev_list_ptr);
	}
	return ;
out:
	kernel_free(dev_ptr);		
	ib_dealloc_device(ib_device);
}

/* 
 *	this function will clean the rdma resources
 *	include the rdma pd resource
 */
void dhmp_dev_list_destroy(struct list_head *dev_list_ptr)
{
	struct dhmp_device *dev_tmp_ptr, *dev_next_tmp_ptr;
	list_for_each_entry_safe(dev_tmp_ptr, 
						dev_next_tmp_ptr, 
						dev_list_ptr, 
						dev_entry)
	{
		if(dev_tmp_ptr->ib_device)
			ib_dealloc_pd ( dev_tmp_ptr->pd );
		vfree(dev_tmp_ptr);
	}
}

