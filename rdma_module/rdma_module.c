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
	server=(struct dhmp_server *)kernel_malloc(sizeof(struct dhmp_server));
	if(!server)
		ERROR_LOG("kernel_malloc error!");
	INFO_LOG("Hello server %d! \n",model_num);

	INIT_LIST_HEAD(&server->dev_list);
	INFO_LOG("INIT_LIST_HEAD");

	dhmp_dev_list_init(ib_device, &server->dev_list); //ib_alloc_pd
	INFO_LOG("dhmp_dev_list_init");


	server->listen_trans=dhmp_transport_create(&server->ctx,
											dhmp_get_dev_from_server(),
											true, false); //rdma
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

static void amper_remove_one(struct ib_device * ib_device)
{

}

static struct ib_client amper_server = {
	.name = "amper_server",
	.add = amper_add_one,
	.remove = amper_remove_one
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

const char* dhmp_wc_opcode_str(enum ib_wc_opcode opcode)
{
	switch(opcode)
	{
		case IB_WC_SEND:
			return "IB_WC_SEND";
		case IB_WC_RDMA_WRITE:
			return "IB_WC_RDMA_WRITE";
		case IB_WC_RDMA_READ:
			return "IB_WC_RDMA_READ";
		case IB_WC_COMP_SWAP:
			return "IB_WC_COMP_SWAP";
		case IB_WC_FETCH_ADD:
			return "IB_WC_FETCH_ADD";
		case IB_WC_BIND_MW:
			return "IB_WC_BIND_MW";
		case IB_WC_RECV:
			return "IB_WC_RECV";
		case IB_WC_RECV_RDMA_WITH_IMM:
			return "IB_WC_RECV_RDMA_WITH_IMM";
		default:
			return "IB_WC_UNKNOWN";
	};
}

static void dhmp_wc_success_handler(struct ib_wc* wc)
{
	INFO_LOG("dhmp_wc_success_handler");
}

static void dhmp_wc_error_handler(struct ib_wc* wc)
{
		ERROR_LOG("wc status is ERROR");
}




module_init(server_init);
module_exit(server_exit);
MODULE_LICENSE("GPL");