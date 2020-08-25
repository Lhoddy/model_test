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

struct dhmp_client *client=NULL;
int rdelay,wdelay,knum;

static struct dhmp_transport* dhmp_node_select()
{
	int i;
	
	for(i=0; i<DHMP_SERVER_NODE_NUM; i++)
	{
		if(client->fifo_node_index>=DHMP_SERVER_NODE_NUM)
			client->fifo_node_index=0;

		if(client->connect_trans[client->fifo_node_index]!=NULL &&
			(client->connect_trans[client->fifo_node_index]->trans_state==
				DHMP_TRANSPORT_STATE_CONNECTED))
		{
			++client->fifo_node_index;
			return client->connect_trans[client->fifo_node_index-1];
		}

		++client->fifo_node_index;
	}
	
	return NULL;
}

struct dhmp_transport* dhmp_get_trans_from_addr(void *dhmp_addr)
{
	long long node_index=(long long)dhmp_addr;
	node_index=node_index>>48;
	return client->connect_trans[node_index];
}

void *dhmp_malloc(size_t length, int is_special)
{
	struct dhmp_transport *rdma_trans=NULL;
	struct dhmp_malloc_work malloc_work;
	struct dhmp_work *work;
	struct dhmp_addr_info *addr_info;
	
	if(length<0)
	{
		ERROR_LOG("length is error.");
		goto out;
	}

	/*select which node to alloc nvm memory*/
	rdma_trans=dhmp_node_select();
	if(!rdma_trans)
	{
		ERROR_LOG("don't exist remote server.");
		goto out;
	}

	work=malloc(sizeof(struct dhmp_work));
	if(!work)
	{
		ERROR_LOG("allocate memory error.");
		goto out;
	}
	
	addr_info=malloc(sizeof(struct dhmp_addr_info));
	if(!addr_info)
	{
		ERROR_LOG("allocate memory error.");
		goto out_work;
	}
	addr_info->nvm_mr.length=0;
	
	malloc_work.addr_info=addr_info;
	malloc_work.rdma_trans=rdma_trans;
	malloc_work.length=length;
	malloc_work.done_flag=false;
	malloc_work.is_special = is_special;

	work->work_type=DHMP_WORK_MALLOC;
	work->work_data=&malloc_work;

	pthread_mutex_lock(&client->mutex_work_list);
	list_add_tail(&work->work_entry, &client->work_list);
	pthread_mutex_unlock(&client->mutex_work_list);
	
	while(!malloc_work.done_flag);

	free(work);
	
	return malloc_work.res_addr;

out_work:
	free(work);
out:
	return NULL;
}

void dhmp_free(void *dhmp_addr)
{
	struct dhmp_free_work free_work;
	struct dhmp_work *work;
	struct dhmp_transport *rdma_trans;
	
	if(dhmp_addr==NULL)
	{
		ERROR_LOG("dhmp address is NULL");
		return ;
	}

	rdma_trans=dhmp_get_trans_from_addr(dhmp_addr);
	if(!rdma_trans||rdma_trans->trans_state!=DHMP_TRANSPORT_STATE_CONNECTED)
	{
		ERROR_LOG("rdma connection error.");
		return ;
	}
	
	work=malloc(sizeof(struct dhmp_work));
	if(!work)
	{
		ERROR_LOG("allocate memory error.");
		return ;
	}

	free_work.rdma_trans=rdma_trans;
	free_work.dhmp_addr=dhmp_addr;
	free_work.done_flag=false;
	
	work->work_type=DHMP_WORK_FREE;
	work->work_data=&free_work;

	pthread_mutex_lock(&client->mutex_work_list);
	list_add_tail(&work->work_entry, &client->work_list);
	pthread_mutex_unlock(&client->mutex_work_list);
	
	while(!free_work.done_flag);

	free(work);
}

int dhmp_read(void *dhmp_addr, void * local_buf, size_t count)
{
	struct dhmp_transport *rdma_trans=NULL;
	struct dhmp_rw_work rwork;
	struct dhmp_work *work,*con;
	
	rdma_trans=dhmp_get_trans_from_addr(dhmp_addr);;
	if(!rdma_trans||rdma_trans->trans_state!=DHMP_TRANSPORT_STATE_CONNECTED)
	{
		ERROR_LOG("rdma connection error.");
		return -1;
	}

	work=malloc(sizeof(struct dhmp_work));
	if(!work)
	{
		ERROR_LOG("allocate memory error.");
		return -1;
	}
	
	rwork.done_flag=false;
	rwork.length=count;
	rwork.local_addr=local_buf;
	rwork.dhmp_addr=dhmp_addr;
	rwork.rdma_trans=rdma_trans;
	
	work->work_type=DHMP_WORK_READ;
	work->work_data=&rwork;
	
	con = work;
	//INFO_LOG("work = %p",work);	
	pthread_mutex_lock(&client->mutex_work_list);
	list_add_tail(&work->work_entry, &client->work_list);
	pthread_mutex_unlock(&client->mutex_work_list);

	while(!rwork.done_flag);
	//INFO_LOG("free work start");
		//INFO_LOG("wrok = %p !",con);
		free(con);
		//INFO_LOG("free");
	//INFO_LOG("free over");
	return 0;
}

int dhmp_write(void *dhmp_addr, void * local_buf, size_t count)
{
	struct dhmp_transport *rdma_trans=NULL;
	struct dhmp_rw_work wwork;
	struct dhmp_work *work;

	rdma_trans=dhmp_get_trans_from_addr(dhmp_addr);
	if(!rdma_trans||rdma_trans->trans_state!=DHMP_TRANSPORT_STATE_CONNECTED)
	{
		ERROR_LOG("rdma connection error.");
		return -1;
	}

	work=malloc(sizeof(struct dhmp_work));
	if(!work)
	{
		ERROR_LOG("alloc memory error.");
		return -1;
	}
	wwork.done_flag=false;
	wwork.length=count;
	wwork.local_addr=local_buf;
	wwork.dhmp_addr=dhmp_addr;
	wwork.rdma_trans=rdma_trans;
			
	work->work_type=DHMP_WORK_WRITE;
	work->work_data=&wwork;
	
	pthread_mutex_lock(&client->mutex_work_list);
	list_add_tail(&work->work_entry, &client->work_list);
	pthread_mutex_unlock(&client->mutex_work_list);
	
	while(!wwork.done_flag);

	free(work);
	
	return 0;
}

int dhmp_write2(void *dhmp_addr, void * local_buf, size_t count)
{
	struct dhmp_transport *rdma_trans=NULL;
	struct dhmp_rw2_work wwork;
	struct dhmp_work *work;
	rdma_trans=dhmp_get_trans_from_addr(dhmp_addr);
	if(!rdma_trans||rdma_trans->trans_state!=DHMP_TRANSPORT_STATE_CONNECTED)
	{
		ERROR_LOG("rdma connection error.");
		return -1;
	}

	work=malloc(sizeof(struct dhmp_work));
	if(!work)
	{
		ERROR_LOG("alloc memory error.");
		return -1;
	}
	wwork.done_flag=false;
	wwork.length=count;
	wwork.local_addr=local_buf;
	wwork.dhmp_addr=dhmp_addr;
	wwork.rdma_trans=rdma_trans;
	wwork.S_ringMR = &(client->ringbuffer.mr);	
		
	work->work_type=DHMP_WORK_WRITE2;
	work->work_data=&wwork;
	
	pthread_mutex_lock(&client->mutex_work_list);
	list_add_tail(&work->work_entry, &client->work_list);
	pthread_mutex_unlock(&client->mutex_work_list);
	
	while(!wwork.done_flag);

	free(work);
	
	return 0;
}

int dhmp_send(void *dhmp_addr, void * local_buf, size_t count, bool is_write)
{
	struct dhmp_transport *rdma_trans=NULL;
	struct dhmp_Send_work wwork;
	struct dhmp_work *work;

	rdma_trans=dhmp_get_trans_from_addr(dhmp_addr);
	if(!rdma_trans||rdma_trans->trans_state!=DHMP_TRANSPORT_STATE_CONNECTED)
	{
		ERROR_LOG("rdma connection error.");
		return -1;
	}

	work=malloc(sizeof(struct dhmp_work));
	if(!work)
	{
		ERROR_LOG("alloc memory error.");
		return -1;
	}
	wwork.done_flag=false;
	wwork.recv_flag = false;
	wwork.length=count;
	wwork.rdma_trans=rdma_trans;
	wwork.local_addr = local_buf;
	wwork.is_write = is_write;
	wwork.dhmp_addr = dhmp_addr;
		
	work->work_type=DHMP_WORK_SEND;
	work->work_data=&wwork;
	
	pthread_mutex_lock(&client->mutex_work_list);
	list_add_tail(&work->work_entry, &client->work_list);
	pthread_mutex_unlock(&client->mutex_work_list);
	while(!wwork.done_flag);
	free(work);
	return 0;
}


void *GetAddr_request1(void * dhmp_addr, size_t length, uint32_t * task_offset,  bool * cmpflag)
{
	struct dhmp_transport *rdma_trans=NULL;
	struct reqAddr_work wwork;    
	struct dhmp_work *work;

	rdma_trans=dhmp_get_trans_from_addr(dhmp_addr);
	if(!rdma_trans||rdma_trans->trans_state!=DHMP_TRANSPORT_STATE_CONNECTED)
	{
		ERROR_LOG("rdma connection error.");
		return NULL;
	}

	work=malloc(sizeof(struct dhmp_work));
	if(!work)
	{
		ERROR_LOG("alloc memory error.");
		return NULL;
	}
	wwork.done_flag=false;
	wwork.recv_flag = false;
	wwork.length=length;
	wwork.rdma_trans=rdma_trans;
	wwork.dhmp_addr = dhmp_addr;	
	wwork.cmpflag = cmpflag;  		

	work->work_type=DHMP_WORK_REQADDR1;  
	work->work_data=&wwork;
	
	pthread_mutex_lock(&client->mutex_work_list);
	list_add_tail(&work->work_entry, &client->work_list);
	pthread_mutex_unlock(&client->mutex_work_list);
	
	while(!wwork.done_flag);

	free(work);
	if(task_offset != NULL)	
		*task_offset = (uint32_t)wwork.length;
	return wwork.dhmp_addr;
}

int dhmp_write_imm(void *dhmp_addr, void * local_buf, size_t length, uint32_t task_offset ,bool *cmpflag)
{
	struct dhmp_transport *rdma_trans=NULL;
	struct dhmp_writeImm_work wwork;
	struct dhmp_work *work;

	rdma_trans=dhmp_get_trans_from_addr(dhmp_addr);
	if(!rdma_trans||rdma_trans->trans_state!=DHMP_TRANSPORT_STATE_CONNECTED)
	{
		ERROR_LOG("rdma connection error.");
		return -1;
	}

	work=malloc(sizeof(struct dhmp_work));
	if(!work)
	{
		ERROR_LOG("alloc memory error.");
		return -1;
	}
	wwork.done_flag=false;
	wwork.recv_flag = false;
	wwork.length=length;
	wwork.rdma_trans=rdma_trans;
	wwork.local_addr = local_buf;
	wwork.dhmp_addr = dhmp_addr;	
	wwork.task_offset = task_offset;
		
	work->work_type=DHMP_WORK_WRITEIMM;
	work->work_data=&wwork;
	
	pthread_mutex_lock(&client->mutex_work_list);
	list_add_tail(&work->work_entry, &client->work_list);
	pthread_mutex_unlock(&client->mutex_work_list);
	
	while(!wwork.done_flag);

	free(work);
	
	return 0;
}

int dhmp_write_imm2(void * server_addr,void *dhmp_addr, void * local_buf, size_t length)
{
	struct dhmp_transport *rdma_trans=NULL;
	struct dhmp_writeImm2_work wwork;
	struct dhmp_work *work;

	rdma_trans=dhmp_get_trans_from_addr(dhmp_addr);
	if(!rdma_trans||rdma_trans->trans_state!=DHMP_TRANSPORT_STATE_CONNECTED)
	{
		ERROR_LOG("rdma connection error.");
		return -1;
	}

	work=malloc(sizeof(struct dhmp_work));
	if(!work)
	{
		ERROR_LOG("alloc memory error.");
		return -1;
	}
	wwork.done_flag=false;
	wwork.recv_flag = false;
	wwork.length=length;
	wwork.rdma_trans=rdma_trans;
	wwork.local_addr = local_buf;
	wwork.dhmp_addr = dhmp_addr;	
	wwork.server_addr = server_addr;
	wwork.S_ringMR = &(client->ringbuffer.mr);
			
	work->work_type=DHMP_WORK_WRITEIMM2;
	work->work_data=&wwork;
	
	pthread_mutex_lock(&client->mutex_work_list);
	list_add_tail(&work->work_entry, &client->work_list);
	pthread_mutex_unlock(&client->mutex_work_list);
	
	while(!wwork.done_flag);

	free(work);
	
	return 0;
}

int dhmp_write_imm3(void *dhmp_addr, void * local_buf, size_t length)
{

	struct dhmp_transport *rdma_trans=NULL;
	struct dhmp_writeImm3_work wwork;
	struct dhmp_work *work;

	rdma_trans=dhmp_get_trans_from_addr(dhmp_addr);
	if(!rdma_trans||rdma_trans->trans_state!=DHMP_TRANSPORT_STATE_CONNECTED)
	{
		ERROR_LOG("rdma connection error.");
		return -1;
	}

	work=malloc(sizeof(struct dhmp_work));
	if(!work)
	{
		ERROR_LOG("alloc memory error.");
		return -1;
	}
	wwork.done_flag=false;
	wwork.recv_flag=false;
	wwork.length=length;
	wwork.rdma_trans=rdma_trans;
	wwork.local_addr = local_buf;
	wwork.dhmp_addr = dhmp_addr;	
	wwork.S_ringMR = &(client->ringbuffer.mr);
		
	work->work_type=DHMP_WORK_WRITEIMM3;
	work->work_data=&wwork;

	pthread_mutex_lock(&client->mutex_work_list);
	list_add_tail(&work->work_entry, &client->work_list);
	pthread_mutex_unlock(&client->mutex_work_list);
	
//	dhmp_WriteImm3_work_handler(work);
	while(!wwork.done_flag);
	free(work);
	return 0;
}

int dhmp_send2(void *server_addr, void *dhmp_addr, void * local_buf, size_t length)
{
	struct dhmp_transport *rdma_trans=NULL;
	struct dhmp_Send2_work wwork;
	struct dhmp_work *work;

	rdma_trans=dhmp_get_trans_from_addr(dhmp_addr);
	if(!rdma_trans||rdma_trans->trans_state!=DHMP_TRANSPORT_STATE_CONNECTED)
	{
		ERROR_LOG("rdma connection error.");
		return -1;
	}

	work=malloc(sizeof(struct dhmp_work));
	if(!work)
	{
		ERROR_LOG("alloc memory error.");
		return -1;
	}
	wwork.done_flag=false;
	wwork.recv_flag = false;
	wwork.length=length;
	wwork.rdma_trans=rdma_trans;
	wwork.local_addr = local_buf;
	wwork.dhmp_addr = dhmp_addr;	
	wwork.server_addr = server_addr;
		
	work->work_type=DHMP_WORK_SEND2;
	work->work_data=&wwork;
	
	pthread_mutex_lock(&client->mutex_work_list);
	list_add_tail(&work->work_entry, &client->work_list);
	pthread_mutex_unlock(&client->mutex_work_list);
	
	while(!wwork.done_flag);

	free(work);
	
	return 0;
}


int dhmp_Sread(void *dhmp_addr, void * local_buf, size_t length)
{
	INFO_LOG("dhmp_Sread");
	struct dhmp_transport *rdma_trans=NULL;
	struct dhmp_Sread_work wwork;
	struct dhmp_work *work;

	rdma_trans=dhmp_get_trans_from_addr(dhmp_addr);
	if(!rdma_trans||rdma_trans->trans_state!=DHMP_TRANSPORT_STATE_CONNECTED)
	{
		ERROR_LOG("rdma connection error.");
		return -1;
	}

	work=malloc(sizeof(struct dhmp_work));
	if(!work)
	{
		ERROR_LOG("alloc memory error.");
		return -1;
	}
	wwork.done_flag=false;
	wwork.recv_flag = false;
	wwork.length=length;
	wwork.rdma_trans=rdma_trans;
	wwork.local_addr = local_buf;
	wwork.dhmp_addr = dhmp_addr;	
		
	work->work_type=DHMP_WORK_SREAD;
	work->work_data=&wwork;
	
	pthread_mutex_lock(&client->mutex_work_list);
	list_add_tail(&work->work_entry, &client->work_list);
	pthread_mutex_unlock(&client->mutex_work_list);
	
	while(!wwork.done_flag);

	free(work);
	
	return 0;
}



struct dhmp_device *dhmp_get_dev_from_client()
{
	struct dhmp_device *res_dev_ptr=NULL;
	if(!list_empty(&client->dev_list))
	{
		res_dev_ptr=list_first_entry(&client->dev_list,
									struct dhmp_device,
									dev_entry);
	}
		
	return res_dev_ptr;
}


void dhmp_client_init(size_t size,int obj_num)
{
	int i;
	struct itimerspec poll_its;
	
	client=(struct dhmp_client *)malloc(sizeof(struct dhmp_client));
	if(!client)
	{
		ERROR_LOG("alloc memory error.");
		return ;
	}

	dhmp_hash_init();
	dhmp_config_init(&client->config, true);
	dhmp_context_init(&client->ctx);
	knum=client->config.simu_infos[0].knum;
	
	/*init list about rdma device*/
	INIT_LIST_HEAD(&client->dev_list);
	dhmp_dev_list_init(&client->dev_list);

	/*init FIFO node select algorithm*/
	client->fifo_node_index=0;


	/*init the addr hash table of client*/
	for(i=0;i<DHMP_CLIENT_HT_SIZE;i++)
	{
		INIT_HLIST_HEAD(&client->addr_info_ht[i]);
	}

	
	/*init the structure about send mr list */
	pthread_mutex_init(&client->mutex_send_mr_list, NULL);
	INIT_LIST_HEAD(&client->send_mr_list);

	
	/*init normal connection*/
	memset(client->connect_trans, 0, DHMP_SERVER_NODE_NUM*
										sizeof(struct dhmp_transport*));
	for(i=0;i<client->config.nets_cnt;i++)
	{
		INFO_LOG("create the [%d]-th normal transport.",i);
		client->connect_trans[i]=dhmp_transport_create(&client->ctx, 
														dhmp_get_dev_from_client(),
														false,
														false);
		if(!client->connect_trans[i])
		{
			ERROR_LOG("create the [%d]-th transport error.",i);
			continue;
		}
		client->connect_trans[i]->node_id=i;
		dhmp_transport_connect(client->connect_trans[i],
							client->config.net_infos[i].addr,
							client->config.net_infos[i].port);
	}

	for(i=0;i<client->config.nets_cnt;i++)
	{
		if(i == 1) ERROR_LOG("more than one server or client");
		if(client->connect_trans[i]==NULL)
			continue;
		while(client->connect_trans[i]->trans_state<DHMP_TRANSPORT_STATE_CONNECTED);
		client->per_ops_mr_addr = malloc(size+1024);//request+data
		client->per_ops_mr =dhmp_create_mr_per_ops(client->connect_trans[i], client->per_ops_mr_addr, size+1024);
		client->per_ops_mr_addr2 = malloc(size+1024);//request+data
		client->per_ops_mr2 =dhmp_create_mr_per_ops(client->connect_trans[i], client->per_ops_mr_addr2, size+1024);
	}

	
	
	/*init the structure about work thread*/
	pthread_mutex_init(&client->mutex_work_list, NULL);
	INIT_LIST_HEAD(&client->work_list);
	pthread_create(&client->work_thread, NULL, dhmp_work_handle_thread, (void*)client);

	dhmp_malloc(0,1);// ringbuffer
	
}

static void dhmp_close_connection(struct dhmp_transport *rdma_trans)
{
	struct dhmp_close_work close_work;
	struct dhmp_work *work;

	if(rdma_trans==NULL ||
		rdma_trans->trans_state!=DHMP_TRANSPORT_STATE_CONNECTED)
		return ;
	
	work=malloc(sizeof(struct dhmp_work));
	if(!work)
	{
		ERROR_LOG("allocate memory error.");
		return ;
	}

	close_work.rdma_trans=rdma_trans;
	close_work.done_flag=false;
	
	work->work_type=DHMP_WORK_CLOSE;
	work->work_data=&close_work;

	pthread_mutex_lock(&client->mutex_work_list);
	list_add_tail(&work->work_entry, &client->work_list);
	pthread_mutex_unlock(&client->mutex_work_list);
	
	while(!close_work.done_flag);

	free(work);
}

void dhmp_client_destroy()
{
	int i;
	INFO_LOG("send all disconnect start.");
	for(i=0;i<client->config.nets_cnt;i++)
	{
		dhmp_close_connection(client->connect_trans[i]);
	}
	
	
	for(i=0;i<client->config.nets_cnt;i++)
	{
		if(client->connect_trans[i]==NULL)
			continue;
		while(client->connect_trans[i]->trans_state==DHMP_TRANSPORT_STATE_CONNECTED);
	}


	client->ctx.stop=true;
	
	INFO_LOG("client destroy start.");
	pthread_join(client->ctx.epoll_thread, NULL);
	INFO_LOG("client destroy end.");
	
	free(client);
}


void model_A_write(void * globle_addr, size_t length, void * local_addr)
{
	void * server_addr = GetAddr_request1(globle_addr, length, NULL,NULL);
	dhmp_write(server_addr, local_addr, length);
}

void model_A_writeImm(void * globle_addr, size_t length, void * local_addr)
{
	uint32_t task_offset;
	bool cmpflag = false;
	void * server_addr = GetAddr_request1(globle_addr, length, &task_offset, &cmpflag);
	dhmp_write_imm(server_addr, local_addr, length, task_offset, &cmpflag); 
}

void model_B_write(void * globle_addr, size_t length, void * local_addr)
{
	dhmp_write2(globle_addr, local_addr, length);
}

void model_B_writeImm(void * globle_addr, size_t length, void * local_addr)
{
	dhmp_write_imm3(globle_addr, local_addr, length);
}

void model_B_send(void * globle_addr, size_t length, void * local_addr)
{
	dhmp_send(globle_addr, local_addr, length, 1);
}

void model_C_sread(void * globle_addr, size_t length, void * local_addr)
{
	dhmp_Sread(globle_addr, local_addr, length);
}

void model_D_write(void * server_addr, size_t length, void * local_addr)
{
	dhmp_write(server_addr, local_addr, length);
}

void model_D_writeImm(void * server_addr, size_t length, void * local_addr)
{
	void * globle_addr = server_addr; /*just one server*/
	dhmp_write_imm2(server_addr, globle_addr, local_addr, length); 
}

void model_D_send(void * server_addr, size_t length, void * local_addr)
{
	void * globle_addr = server_addr; /*just one server*/
	dhmp_send2(server_addr, globle_addr, local_addr, length); 
}

void model_1_octopus(void * globle_addr, size_t length, void * local_addr)
{
	void * server_addr = GetAddr_request1(globle_addr, length, NULL,NULL);
	dhmp_write(server_addr, local_addr, length);
}

void model_1_clover(void * globle_addr, size_t length, void * local_addr)
{
	//void * server_addr = GetAddr_request1(globle_addr, length, NULL,NULL); //忽略分配过程或者write N次后RPC一次
	dhmp_write(globle_addr, local_addr, length);
	clover_compare_and_set(globle_addr,client->clover);
}

void model_4_RFP(void * globle_addr, size_t length, void * local_addr)
{
	//void * server_addr = GetAddr_request1(globle_addr, length, NULL,NULL); 
	amper_write_1(client->RFP.write_mr, local_addr, length);
	amper_read_1(client->RFP.read_mr, 256);
}

void model_5_L5(void * globle_addr, size_t length, void * local_addr)
{
	amper_write_2(&(client->L5.mailbox_mr), client->L5.mailbox_offset, 1, 1, &(client->L5.message_mr), 0, local_addr, length);
}

void *dhmp_malloc_messagebuffer(size_t length, int is_special)
{
	struct dhmp_transport *rdma_trans=NULL;
	struct dhmp_malloc_work malloc_work;
	struct dhmp_work *work;
	struct dhmp_addr_info *addr_info;
	
	if(length<0)
	{
		ERROR_LOG("length is error.");
		goto out;
	}
	/*select which node to alloc nvm memory*/
	rdma_trans=dhmp_node_select();
	if(!rdma_trans)
	{
		ERROR_LOG("don't exist remote server.");
		goto out;
	}

	work=malloc(sizeof(struct dhmp_work));
	if(!work)
	{
		ERROR_LOG("allocate memory error.");
		goto out;
	}
	
	addr_info=malloc(sizeof(struct dhmp_addr_info));
	if(!addr_info)
	{
		ERROR_LOG("allocate memory error.");
		goto out_work;
	}
	addr_info->nvm_mr.length=0;
	
	malloc_work.addr_info=addr_info;
	malloc_work.rdma_trans=rdma_trans;
	malloc_work.length=length;
	malloc_work.done_flag=false;
	malloc_work.is_special = is_special;

	work->work_type=DHMP_WORK_MALLOC;
	work->work_data=&malloc_work;

	pthread_mutex_lock(&client->mutex_work_list);
	list_add_tail(&work->work_entry, &client->work_list);
	pthread_mutex_unlock(&client->mutex_work_list);
	
	while(!malloc_work.done_flag);

	free(work);
	
	return malloc_work.res_addr;

out_work:
	free(work);
out:
	return NULL;
}