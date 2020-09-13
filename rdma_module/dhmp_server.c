#include "dhmp.h"
#include "dhmp_log.h"
#include "dhmp_hash.h"
#include "dhmp_context.h"
#include "dhmp_dev.h"
#include "dhmp_transport.h"
#include "dhmp_task.h"
#include "dhmp_work.h"
#include "dhmp_client.h"
#include "dhmp_server.h"

// static char *who = "linux ss";
static int model_num = 1;
static int inet_port = 2333;
module_param(model_num, int, S_IRUGO);
module_param(inet_port, int, S_IRUGO);
struct dhmp_server *server=NULL;

struct dhmp_device *dhmp_get_dev_from_server(void)
{
	struct dhmp_device *res_dev_ptr=NULL;
	if(!list_empty(&server->dev_list))
	{
		res_dev_ptr=list_first_entry(&server->dev_list,
									struct dhmp_device,
									dev_entry);
	}
		
	return res_dev_ptr;
}

static void amper_add_one(struct ib_device * ib_device)
{
	int err = 0;
	struct dhmp_device *dev;
	server=(struct dhmp_server *)kernel_malloc(sizeof(struct dhmp_server));
	if(!server)
		ERROR_LOG("kernel_malloc error!");
	INFO_LOG("Hello server %d! \n",model_num);

	INIT_LIST_HEAD(&server->dev_list);
	INFO_LOG("INIT_LIST_HEAD");

	dhmp_dev_list_init(ib_device, &server->dev_list); //ib_alloc_pd
	INFO_LOG("dhmp_dev_list_init");

	dev =  dhmp_get_dev_from_server();
	if(dev->ib_device != ib_device)
		ERROR_LOG("dhmp_get_dev_from_server error");
	server->mr = ib_alloc_mr(dev->pd, IB_MR_TYPE_SG_GAPS ,IB_ACCESS_LOCAL_WRITE | IB_ACCESS_REMOTE_WRITE | IB_ACCESS_REMOTE_READ | IB_ACCESS_REMOTE_ATOMIC);
	server->listen_trans=dhmp_transport_create(dev, true, false); //rdma
	if(!server->listen_trans)
	{
		ERROR_LOG("create rdma transport error.");
	}
	INFO_LOG("create rdma transport success");

	err=dhmp_transport_listen(server->listen_trans, inet_port);  //rdma
	if(err)
		ERROR_LOG("dhmp_transport_listen_UD error.");
	INFO_LOG("dhmp_transport_listen_UD success");

	// pthread_join(server->ctx.epoll_thread, NULL);
	kfree(server);
	INFO_LOG("server destroy end.");
    return ;
}

// static void amper_remove_one(struct ib_device * ib_device)
// {

// }

static struct ib_client amper_server = {
	.name = "amper_server",
	.add = amper_add_one,
	// .remove = amper_remove_one
};

static int __init server_init(void)
{
	int ret;

    ret = ib_register_client(&amper_server);
    if(ret){
        printk(KERN_ALERT "KERN_ERR Failed to register IB client\n");
    }
    printk(KERN_ALERT "lKERN_INFO Successfully registered server_init module \n");
    return 0;
}

static void __exit server_exit(void)
{
	INFO_LOG("server_exit Goodbye");
}

module_init(server_init);
module_exit(server_exit);
MODULE_LICENSE("GPL");