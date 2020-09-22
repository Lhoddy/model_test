#include "dhmp.h"
#include "dhmp_log.h"
#include "dhmp_hash.h"
#include "dhmp_config.h"
#include "dhmp_context.h"
#include "dhmp_dev.h"
#include "dhmp_transport.h"
#include "dhmp_task.h"
#include "dhmp_work.h"
#include "dhmp_client.h"
#include "dhmp_server.h"
//4KB:4096,8192,16384,32768,65536,131072,262144
//8KB:8192,16384,32768,65536,131072,262144,524288
//16KB:16384,32768,65536,131072,262144,524288
//32KB:32768,65536,131072,262144,524288,1048576
//64KB:65536,131072,262144,524288,1048576,2097152
//128KB:131072,262144,524288,1048576,2097152,4194304
//256KB:262144,524288,1048576,2097152,4194304,8388608
//512KB:524288,1048576,2097152,4194304,8388608,16777216
//1MB:1048576,2097152,4194304,8388608,16777216,33554432
//2MB:2097152,4194304,8388608,16777216,33554432,67108864
//3MB:3145728,6291456,12582912,25165824,50331648,100663296
//4MB:4194304,8388608,16777216,33554432,67108864,134217728
//5MB:5242880,10485760,20971520,41943040,83886080,167772160
//6MB:6291456,12582912,25165824,50331648,100663296,201326592
//8MB:8388608,16777216,33554432,67108864,134217728,268435456
//32MB:33554432,67108864,134217728,268435456,536870912
//64MB:67108864,134217728,268435456,536870912,1073741824,2147483648

struct dhmp_server *server=NULL;


/**
 *	dhmp_get_dev_from_server:get the dev_ptr from dev_list of server.
 */
struct dhmp_device *dhmp_get_dev_from_server()
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


struct dhmp_area *dhmp_area_create(size_t length)
{
	void *addr=NULL;
	struct dhmp_area *area=NULL;
	struct ibv_mr *mr;
	struct dhmp_device *dev;
	bool res;
	
	/*nvm memory*/
	addr=nvm_malloc(length);
	if(!addr)
	{
		ERROR_LOG("allocate nvm memory error.");
		return NULL;
	}

	dev=dhmp_get_dev_from_server();
	mr=ibv_reg_mr(dev->pd,
				addr, length, 
				IBV_ACCESS_LOCAL_WRITE|
				IBV_ACCESS_REMOTE_READ|
				IBV_ACCESS_REMOTE_WRITE|
				IBV_ACCESS_REMOTE_ATOMIC);
	if(!mr)
	{
		ERROR_LOG("ib register mr error.");
		goto out_addr;
	}

	area=malloc(sizeof(struct dhmp_area));//numa_alloc_onnode(sizeof(struct dhmp_area),3);
	if(!area)
	{
		ERROR_LOG("allocate memory error.");
		goto out_mr;
	}

	area->mr=mr;

		list_add(&area->area_entry, &server->area_list[0]);
	
	return area;

out_area:
	free(area);
out_mr:
	ibv_dereg_mr(mr);
out_addr:
	free(addr);
	return NULL;
}


struct ibv_mr * dhmp_malloc_poll_area(size_t length)
{
	void *addr=NULL;
	struct ibv_mr *mr;
	struct dhmp_device *dev;
	bool res;
	
	/*nvm memory*/
	addr = nvm_malloc(length);
	if(!addr)
	{
		ERROR_LOG("allocate nvm memory error.");
		return NULL;
	}

	dev=dhmp_get_dev_from_server();
	mr=ibv_reg_mr(dev->pd,
				addr, length, 
				IBV_ACCESS_LOCAL_WRITE|
				IBV_ACCESS_REMOTE_READ|
				IBV_ACCESS_REMOTE_WRITE|
				IBV_ACCESS_REMOTE_ATOMIC);
	if(!mr)
	{
		ERROR_LOG("ib register mr error.");
		free(addr);
		return NULL;
	}
	memset(addr, 0 ,length);
	server->ringbufferAddr = addr;
	return mr;
}

void *model_B_write_run()
{
	INFO_LOG("start epoll");
	while(1)
	{
		if(server->ringbufferAddr == NULL) continue;
		if((*((bool *)(server->ringbufferAddr))) == true)
		{
			INFO_LOG("get new model_B message");
			dhmp_Write2_request_handler();
			*((bool *)(server->ringbufferAddr)) = false;
		}
	}
	return NULL;
}

void *model_C_Sread_run()
{
	while(1)
	{
		if(server->model_C_new_msg == false) continue;
		INFO_LOG("get new model_C message");
		dhmp_Sread_server();
		server->model_C_new_msg = false;
	}
	return NULL;
}

void *L5_run()
{
	INFO_LOG("start L5 epoll");
	int i = 0;
	while(1)
	{
		if(server->L5_mailbox.addr == NULL) continue;
		if((*((char *)(server->L5_mailbox.addr + i))) != 0)
		{
			INFO_LOG("get new L5 message");
			*((char *)(server->L5_mailbox.addr + i)) = 0;
			//amper_L5_request_handler(); not need
		}
		i = (i+1) % server->client_num;
	}
	return NULL;
}

void amper_scalable_request_handler(int node_id, size_t size)
{
	struct dhmp_task* scalable_task;
	scalable_task = dhmp_read_task_create(server->connect_trans[node_id], NULL, 0);
	if(!scalable_task)
	{
		ERROR_LOG("allocate memory error.");
		return;
	}
	amper_post_read(scalable_task, &(server->Salable[node_id].Cdata_mr), server->Salable[node_id].Sdata_mr->addr, size, server->Salable[node_id].Sdata_mr->lkey, false);
	while(!scalable_task->done_flag);
	_mm_clflush(server->Salable[node_id].Sdata_mr->addr);

	struct dhmp_task* scalable_task2;
	scalable_task2 = dhmp_write_task_create(server->connect_trans[node_id], NULL, 0);
	if(!scalable_task2)
	{
		ERROR_LOG("allocate memory error.");
		return;
	}
	uint64_t num = 0;
	amper_post_write(scalable_task2, &(server->Salable[node_id].Creq_mr), &num, sizeof(uint64_t), 0, true);
	while(!scalable_task2->done_flag);
	return;
}

void *scalable_run(void* arg1)
{
	int node_id = *(int*) arg1;
	INFO_LOG("start scalable %d epoll", node_id);
	int * addr = server->Salable[node_id].Sreq_mr->addr;
	int i = 0;
	while(1)
	{
		if((int)addr[0])
		{
			addr[0] = 0;
			INFO_LOG("get new scalable %d message", (int)addr[0]);
			amper_scalable_request_handler(node_id, *(size_t*)(addr+4)); 
			
		}
	}
	return ;
}


void dhmp_server_init()
{
	int i,err=0;
	
	server=(struct dhmp_server *)malloc(sizeof(struct dhmp_server));
	if(!server)
	{
		ERROR_LOG("allocate memory error.");
		return ;
	}
	
	dhmp_hash_init();
	dhmp_config_init(&server->config, false);
	dhmp_context_init(&server->ctx);

	/*init client transport list*/
	server->cur_connections=0;
	pthread_mutex_init(&server->mutex_client_list, NULL);
	INIT_LIST_HEAD(&server->client_list);
	
	/*init list about rdma device*/
	INIT_LIST_HEAD(&server->dev_list);
	dhmp_dev_list_init(&server->dev_list);


	for(i=0;i<DHMP_CLIENT_HT_SIZE;i++)
	{
		INIT_HLIST_HEAD(&server->addr_info_ht[i]);
	}

	/*init the structure about memory count*/
	/*get dram total size, get nvm total size*/
	server->nvm_total_size=numa_node_size(1, NULL);

	server->ringbufferAddr = NULL;
	server->cur_addr = 0;
	server->nvm_used_size=0;
	INFO_LOG("server nvm total size %ld",server->nvm_total_size);

	server->listen_trans=dhmp_transport_create(&server->ctx,
											dhmp_get_dev_from_server(),
											true, false);
	if(!server->listen_trans)
	{
		ERROR_LOG("create rdma transport error.");
		exit(-1);
	}

	//##############initial
	server->client_num = 0;
	

#ifdef DaRPC_SERVER
	for(i=0; i<DaRPC_clust_NUM; i++)
		server->DaRPC_dcq[i] = NULL;
#endif
	server->L5_mailbox.addr == NULL;


#ifdef UD
	err=dhmp_transport_listen_UD(server->listen_trans,
					server->config.net_infos[server->config.curnet_id].port);
#else
	err=dhmp_transport_listen(server->listen_trans,
					server->config.net_infos[server->config.curnet_id].port);
#endif
	if(err)
		exit(- 1);

	/*create one area and init area list*/	
	INIT_LIST_HEAD(&server->area_list[0]);
	// server->cur_area=dhmp_area_create(2097152);

#ifdef model_B
	pthread_create(&server->model_B_write_epoll_thread, NULL, model_B_write_run, NULL);
#endif
#ifdef model_C
	pthread_create(&server->model_C_Sread_epoll_thread, NULL, model_C_Sread_run, NULL);
#endif

#ifdef L5
	pthread_create(&server->L5_poll_thread, NULL, L5_run, NULL);
#endif	
}

void dhmp_server_destroy()
{
	INFO_LOG("server destroy start.");
#ifdef model_B
	pthread_join(server->model_B_write_epoll_thread, NULL);
#endif
#ifdef model_C
	pthread_join(server->model_C_Sread_epoll_thread, NULL);
#endif	
	pthread_join(server->ctx.epoll_thread, NULL);

	int i;
#ifdef L5
	pthread_join(server->L5_poll_thread, NULL);
#endif	
#ifdef scalable
	for(i =0;i<DHMP_CLIENT_NODE_NUM ;i++)
		pthread_join(server->scalable_poll_thread[i], NULL);
#endif
	INFO_LOG("server destroy end.");
	free(server);
}


void amper_allocspace_for_server(struct dhmp_transport* rdma_trans, int is_special, size_t size)
{
	int node_id = rdma_trans->node_id;
	struct dhmp_send_mr* temp_smr;
	void * temp;
	switch(is_special)
	{
		case 3: // L5
		{
			if(node_id == 0) //for test
			{
				server->L5_mailbox.addr = nvm_malloc(DHMP_CLIENT_NODE_NUM*sizeof(char)); // store client num  
				temp_smr = dhmp_create_smr_per_ops(rdma_trans, server->L5_mailbox.addr, DHMP_CLIENT_NODE_NUM*sizeof(char));
				server->L5_mailbox.mr = temp_smr->mr;
				temp = nvm_malloc(sizeof(char)); // store client num  
				temp_smr = dhmp_create_smr_per_ops(rdma_trans, temp, sizeof(char));
				server->L5_mailbox.read_mr = temp_smr->mr;
			}
			//only use server's first dev
			server->L5_message[node_id].addr = nvm_malloc(size);
			temp_smr = dhmp_create_smr_per_ops(rdma_trans, server->L5_message[node_id].addr, size);
			server->L5_message[node_id].mr = temp_smr->mr;
			INFO_LOG("L5 buffer %d %p is ready.",node_id,server->L5_mailbox.mr->addr);
			
		}
		break;
		case 4: // for tailwind
		{
			server->Tailwind_buffer[node_id].addr = nvm_malloc(size); 
			temp_smr = dhmp_create_smr_per_ops(rdma_trans, server->Tailwind_buffer[node_id].addr, size);
			server->Tailwind_buffer[node_id].mr = temp_smr->mr;

			temp = nvm_malloc(sizeof(char)); 
			temp_smr = dhmp_create_smr_per_ops(rdma_trans, temp, sizeof(char));
			server->Tailwind_buffer[node_id].read_mr = temp_smr->mr;
		}
		break;
		case 5: // for DaRPC only need once
		{
			temp = nvm_malloc(sizeof(char)); 
			temp_smr = dhmp_create_smr_per_ops(rdma_trans, temp, sizeof(char));
			server->read_mr = temp_smr->mr;
		}
		break;
		case 6: // for RFP
		{
			temp = nvm_malloc(size); 
			temp_smr = dhmp_create_smr_per_ops(rdma_trans, temp, size);
			server->RFP[node_id].write_mr = temp_smr->mr;

			temp = nvm_malloc(size); 
			temp_smr = dhmp_create_smr_per_ops(rdma_trans, temp, size);
			server->RFP[node_id].read_mr = temp_smr->mr;
		}
		case 7: // for scalable
		{
			size_t write_length = sizeof(void *) + sizeof(size_t); // client_addr+req_size  
			size_t totol_length = BATCH * (size + sizeof(void*) + sizeof(size_t) + 1);//(data+remote_addr+size+vaild )* batch
			temp = nvm_malloc(write_length); 
			memset(temp, 0 ,write_length);
			temp_smr = dhmp_create_smr_per_ops(rdma_trans, temp, write_length);
			server->Salable[node_id].Sreq_mr = temp_smr->mr;

			temp = nvm_malloc(totol_length); 
			temp_smr = dhmp_create_smr_per_ops(rdma_trans, temp, totol_length);
			server->Salable[node_id].Sdata_mr = temp_smr->mr;
			pthread_create(&(server->scalable_poll_thread[node_id]), NULL, scalable_run, &node_id);
		}
	};
	return;
}