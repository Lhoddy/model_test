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

struct dhmp_transport* dhmp_get_trans_from_addr(void *dhmp_addr)
{
	return client->connect_trans;
}

void *dhmp_malloc(size_t length, int is_special)
{
	struct dhmp_malloc_work malloc_work;
	struct dhmp_work *work;
	struct dhmp_addr_info *addr_info;
	
	if(length<0)
	{
		ERROR_LOG("length is error.");
		goto out;
	}

	/*select which node to alloc nvm memory*/
	if(!client->connect_trans)
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
	malloc_work.rdma_trans= client->connect_trans;
	malloc_work.length=length;
	malloc_work.done_flag=false;
	malloc_work.is_special = is_special;
	malloc_work.recv_flag=false;

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


int amper_clover_compare_and_set(void *dhmp_addr, size_t length, uintptr_t value)
{
	struct dhmp_transport *rdma_trans=NULL;
	struct amper_clover_work wwork;
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
	wwork.dhmp_addr=dhmp_addr;
	wwork.rdma_trans=rdma_trans;
	wwork.length = length;
	wwork.value =value;
			
	work->work_type=AMPER_WORK_CLOVER;
	work->work_data=&wwork;
	
	pthread_mutex_lock(&client->mutex_work_list);
	list_add_tail(&work->work_entry, &client->work_list);
	pthread_mutex_unlock(&client->mutex_work_list);
	
	while(!wwork.done_flag);

	free(work);
	
	return 0;
}

int amper_write_L5( void * local_buf, size_t count, uintptr_t globle_addr, char flag_write);
{
	struct dhmp_transport *rdma_trans= client->connect_trans; // assume only one server
	struct amper_L5_work wwork;
	struct dhmp_work *work;

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
	wwork.rdma_trans=rdma_trans;
	wwork.dhmp_addr = globle_addr;
	wwork.flag_write = flag_write;
			
	work->work_type=AMPER_WORK_L5;
	work->work_data=&wwork;
	
	pthread_mutex_lock(&client->mutex_work_list);
	list_add_tail(&work->work_entry, &client->work_list);
	pthread_mutex_unlock(&client->mutex_work_list);
	
	while(!wwork.done_flag);

	free(work);
	
	return 0;
}

int amper_scalable(size_t count, int write_type)
{
	struct dhmp_transport *rdma_trans= client->connect_trans; // assume only one server
	struct amper_scalable_work wwork;
	struct dhmp_work *work;

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
	wwork.write_type = write_type;
	wwork.rdma_trans=rdma_trans;
			
	work->work_type=AMPER_WORK_scalable;
	work->work_data=&wwork;
	
	pthread_mutex_lock(&client->mutex_work_list);
	list_add_tail(&work->work_entry, &client->work_list);
	pthread_mutex_unlock(&client->mutex_work_list);
	
	while(!wwork.done_flag);

	free(work);
	
	return 0;
}

int amper_sendRPC_Tailwind(size_t offset, void * local_buf, size_t count)
{
	struct dhmp_transport *rdma_trans= client->connect_trans; // assume only one server
	struct amper_Tailwind_work wwork;
	struct dhmp_work *work;

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
	wwork.offset = offset;
	wwork.local_addr=local_buf;
	wwork.rdma_trans=rdma_trans;
			
	work->work_type=AMPER_WORK_Tailwind_RPC;
	work->work_data=&wwork;
	
	pthread_mutex_lock(&client->mutex_work_list);
	list_add_tail(&work->work_entry, &client->work_list);
	pthread_mutex_unlock(&client->mutex_work_list);
	
	while(!wwork.done_flag);

	free(work);
	
	return 0;
}




int amper_write_Tailwind(size_t offset, void * local_buf, size_t count)
{
	struct dhmp_transport *rdma_trans= client->connect_trans; // assume only one server
	struct amper_Tailwind_work wwork;
	struct dhmp_work *work;

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
	wwork.offset = offset;
	wwork.local_addr=local_buf;
	wwork.rdma_trans=rdma_trans;
			
	work->work_type=AMPER_WORK_Tailwind;
	work->work_data=&wwork;
	
	pthread_mutex_lock(&client->mutex_work_list);
	list_add_tail(&work->work_entry, &client->work_list);
	pthread_mutex_unlock(&client->mutex_work_list);
	
	while(!wwork.done_flag);

	free(work);
	
	return 0;
}


int amper_sendRPC_DaRPC(void * local_buf, size_t count)
{
	struct dhmp_transport *rdma_trans= client->connect_trans; // assume only one server
	struct amper_DaRPC_work wwork;
	struct dhmp_work *work;

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
	wwork.local_addr=local_buf;
	wwork.rdma_trans=rdma_trans;
			
	work->work_type=AMPER_WORK_DaRPC;
	work->work_data=&wwork;
	
	while(1)
	{
		pthread_mutex_lock(&client->mutex_request_num);
		if(client->para_request_num == 0)
		{
			pthread_mutex_unlock(&client->mutex_request_num);
			continue;
		}
		client->para_request_num --;
		pthread_mutex_unlock(&client->mutex_request_num);
		break;
	}

	pthread_mutex_lock(&client->mutex_work_list);
	list_add_tail(&work->work_entry, &client->work_list);
	pthread_mutex_unlock(&client->mutex_work_list);
	
	while(!wwork.done_flag);

	free(work);
	
	return 0;
}


int amper_RFP(void * local_buf, uintptr_t globle_addr, size_t count, bool is_write)
{
	struct dhmp_transport *rdma_trans= client->connect_trans;
	struct dhmp_RFP_work wwork;
	struct dhmp_work *work;
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
	wwork.rdma_trans=rdma_trans;
	wwork.is_write = is_write;
	wwork.dhmp_addr = globle_addr;
			
	work->work_type=AMPER_WORK_RFP;
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
	
	client=(struct dhmp_client *)malloc(sizeof(struct dhmp_client));
	if(!client)
	{
		ERROR_LOG("alloc memory error.");
		return ;
	}

	dhmp_hash_init();
	dhmp_config_init(&client->config, true);
	dhmp_context_init(&client->ctx);
	
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
		client->connect_trans=dhmp_transport_create(&client->ctx, 
														dhmp_get_dev_from_client(),
														false,
														false);
		if(!client->connect_trans)
		{
			ERROR_LOG("create the [%d]-th transport error.",i);
			continue;
		}
		client->connect_trans->node_id=i;
#ifdef UD
		dhmp_transport_connect_UD(client->connect_trans,
							client->config.net_infos[i].addr,
							client->config.net_infos[i].port);
#else
		dhmp_transport_connect(client->connect_trans,
							client->config.net_infos[i].addr,
							client->config.net_infos[i].port);
#endif
	}

	for(i=0;i<client->config.nets_cnt;i++)
	{
		if(i == 1) ERROR_LOG("more than one server or client");
		if(client->connect_trans==NULL)
			continue;
		while(client->connect_trans->trans_state<DHMP_TRANSPORT_STATE_CONNECTED);
		void * temp = malloc(8);
		client->cas_mr = dhmp_create_smr_per_ops(client->connect_trans, temp, 8);

		client->per_ops_mr_addr = zalloc(size+1024);//request+data
		client->per_ops_mr =dhmp_create_smr_per_ops(client->connect_trans, client->per_ops_mr_addr, size+1024);
		client->per_ops_mr_addr2 = zalloc(size+1024);//request+data
		client->per_ops_mr2 =dhmp_create_smr_per_ops(client->connect_trans, client->per_ops_mr_addr2, size+1024);
	}


	
	/*init the structure about work thread*/
	pthread_mutex_init(&client->mutex_work_list, NULL);
	pthread_mutex_init(&client->mutex_request_num, NULL);
	INIT_LIST_HEAD(&client->work_list);
	pthread_create(&client->work_thread, NULL, dhmp_work_handle_thread, (void*)client);

	// dhmp_malloc(0,1);// ringbuffer
	{//scalable
		client->Salable.Creq_mr = client->per_ops_mr->mr;
		client->Salable.Cdata_mr = client->per_ops_mr2->mr;
	}
	{//L5
		client->L5.local_mr = client->per_ops_mr->mr;
		size_t head_size = sizeof(uintptr_t) + sizeof(size_t)+1;
		void *temp_head = malloc(head_size);
		struct dhmp_send_mr* local_smr1 = dhmp_create_smr_per_ops(wwork->rdma_trans, temp_head, head_size);
	}
	client->para_request_num = 5;
	
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
#ifndef UD
	for(i=0;i<client->config.nets_cnt;i++)
	{
		dhmp_close_connection(client->connect_trans);
	}
#endif
	
	for(i=0;i<client->config.nets_cnt;i++)
	{
		if(client->connect_trans==NULL)
			continue;
		while(client->connect_trans->trans_state==DHMP_TRANSPORT_STATE_CONNECTED);
	}


	client->ctx.stop=true;
	
	INFO_LOG("client destroy start.");
	pthread_join(client->ctx.epoll_thread, NULL);
	rdma_leave_multicast(client->connect_trans->cm_id, (struct sockaddr *)&(client->connect_trans->peer_addr));
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
	void * server_addr = GetAddr_request1(globle_addr, length, NULL,NULL); //write imm and lock （imm=node_id+offset）
	dhmp_write(server_addr, local_addr, length);
	amper_clover_compare_and_set(globle_addr, length, NULL); //unlock perfile;we use object  todo
}
void model_1_octopus_R(void * globle_addr, size_t length, void * local_addr)
{
	void * server_addr = GetAddr_request1(globle_addr, length, NULL,NULL); //write imm and no_lock （imm=node_id+offset）
	dhmp_read(server_addr, local_addr, length);
}

void model_1_clover(void * space_addr, size_t length, void * local_addr, uintptr_t* point_addr)//+globle_obj_name
{
	//8byte hander只够存放ptr,忽略GC元数据(8b)
	//void * server_addr = GetAddr_request1(globle_addr, length, NULL,NULL); //write N次后RPC一次
	dhmp_write(space_addr, local_addr, length);// local_addr = data + 0
	amper_clover_compare_and_set((void *)*point_addr, length, (uintptr_t)space_addr);//+globle_obj_name
	memcpy(point_addr,&space_addr , sizeof(uintptr_t));
}
void model_1_clover_R(size_t length, void * local_addr, uintptr_t* point_addr)
{
	void * temp = malloc(length+8);
	dhmp_read((void *)*point_addr, temp, length+8); // read data+point
	while((uintptr_t)*(uintptr_t*)(temp+length) != 0) 
	{	
		//walk_chain; will not run when server_num = 1
		dhmp_read((void *)*(uintptr_t*)(temp+length), temp, length+8);
	}
}

void model_4_RFP( size_t length, void * local_addr, uintptr_t globle_addr, char flag_write)
{
	if(flag_write == 1)		
		amper_RFP(local_addr, globle_addr, length, true);   
	else
		amper_RFP(local_addr, globle_addr, length, false); 
}

void model_5_L5( size_t length, void * local_addr, uintptr_t globle_addr, char flag_write)
{
	amper_write_L5(local_addr, length, globle_addr, flag_write);
}

void model_3_herd(void * globle_addr, size_t length, void * local_addr)
{
	// 1000value - 2status - 16key
	// amper_write_herd(local_addr, length);  // todo
}

void model_6_Tailwind(int accessnum, int obj_num, int *rand_num ,size_t length, void * local_addr)
{
	int i = 0;
	size_t count = sizeof(size_t) + sizeof(uint32_t) + sizeof(uint32_t) + length;//(size_t+check+checksum+data)
	char * tailwind_data = malloc(count);
	// memcpy(tailwind_data,local_addr);

	amper_sendRPC_Tailwind(rand_num[i++], tailwind_data, count);
	for(; i < accessnum-1; i++)
	{
		amper_write_Tailwind(rand_num[i % obj_num], tailwind_data, count); 
	}
	amper_sendRPC_Tailwind(rand_num[i % obj_num], tailwind_data, count);
}

void model_3_DaRPC( size_t length, void * local_addr, uintptr_t globle_addr, char flag_write) //server多线程未做
{
	int i = 0;
	size_t count = sizeof(char) + sizeof(void*) + sizeof(size_t) + length;//to check
	size_t totol_count = count * BATCH;
	char * tailwind_data = malloc(totol_count);
	memcpy(tailwind_data , &length, sizeof(size_t));

	for(; i < accessnum; i = i + BATCH)
		amper_sendRPC_DaRPC(tailwind_data, totol_count); //窗口异步 + read flush
}

//need  client->per_ops_mr_addr + client->per_ops_mr_addr2 > batch*reqsize
void model_7_scalable(int accessnum, int *rand_num , size_t length, void * local_addr)
{
	int i = 0;
	size_t write_length =  BATCH *(sizeof(void *) + sizeof(size_t)); // client_addr+req_size
	int * scalable_write_data = client->per_ops_mr_addr;

	size_t totol_length = BATCH * (length + sizeof(void*) + sizeof(size_t) + 1);//(data+remote_addr+size+vaild )* batch
	char * scalable_request_data = client->per_ops_mr_addr2;

	memcpy(scalable_write_data + sizeof(int) , &totol_length, sizeof(size_t));

	for(;i < accessnum; i= i+BATCH)
	{
		scalable_write_data[0] = i;		
		amper_scalable(write_length ,1); //  write + read & write
		while((int)(scalable_write_data[0]));
		amper_scalable(totol_length ,2); //  write 
	}
	
}



void send_UD(void* local_buf,size_t length )
{
	struct dhmp_UD_work wwork;
	struct dhmp_work *work;

	/*select which node to alloc nvm memory*/
	if(!client->connect_trans)
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
	
	wwork.rdma_trans = client->connect_trans;
	wwork.length=length;
	wwork.done_flag=false;
	wwork.local_buf = local_buf;
	wwork.recv_flag=false;

	work->work_type=AMPER_WORK_UD;
	work->work_data=&wwork;

	{
		struct dhmp_UD_work *wwork;
		struct dhmp_msg msg;
		struct dhmp_UD_request req_msg;
	
		wwork=(struct dhmp_UD_work*)work->work_data;
		
		/*build malloc request msg*/
		req_msg.req_size=wwork->length;
		req_msg.task = wwork;
		
		msg.msg_type=DHMP_MSG_UD_REQUEST;
		msg.data_size=sizeof(struct dhmp_UD_request)+wwork->length;
		
		void *temp = malloc(msg.data_size);
		memcpy(temp, &req_msg ,sizeof(struct dhmp_UD_request));
		memcpy(temp+ sizeof(struct dhmp_UD_request), wwork->local_buf ,wwork->length);
		msg.data=temp;
		amper_ud_post_send(client->connect_trans, &msg);
	
		/*wait for the server return result*/
		while(wwork->recv_flag==false);
		free(temp);
	}
	free(work);
out:
	return ;
}