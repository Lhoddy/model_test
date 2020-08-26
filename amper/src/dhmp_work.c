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
void *dhmp_transfer_dhmp_addr(struct dhmp_transport *rdma_trans, void *normal_addr)
{
		return normal_addr;
}

/**
 * dhmp_hash_in_client:cal the addr into hash key
 */
int dhmp_hash_in_client(void *addr)
{
	uint32_t key;
	int index;

	key=hash(&addr, sizeof(void*));
	/*key is a uint32_t value,through below fomula transfer*/
	index=((key%DHMP_CLIENT_HT_SIZE)+DHMP_CLIENT_HT_SIZE)%DHMP_CLIENT_HT_SIZE;

	return index;
}

/**
 *	dhmp_addr_info_insert_ht:
 *	add new addr info into the addr_info_hashtable in client
 */
static void dhmp_addr_info_insert_ht(void *dhmp_addr,
											struct dhmp_addr_info *addr_info)
{
	int index;

	index=dhmp_hash_in_client(dhmp_addr);
	DEBUG_LOG("insert ht %d %p",index,addr_info->nvm_mr.addr);
	hlist_add_head(&addr_info->addr_entry, &client->addr_info_ht[index]);
}


void dhmp_addr_info_insert_ht_at_Server(void *dhmp_addr,
											struct dhmp_server_addr_info *addr_info)
{
	int index;
	index=dhmp_hash_in_client(dhmp_addr);
	DEBUG_LOG("insert server ht %d %p",index,addr_info->server_addr);
	hlist_add_head(&addr_info->addr_entry, &server->addr_info_ht[index]);
}


int dhmp_get_node_index_from_addr(void *dhmp_addr)
{
	long long node_index=(long long)dhmp_addr;
	int res;
	node_index=node_index>>48;
	res=node_index;
	return res;
}

void dhmp_malloc_work_handler(struct dhmp_work *work)
{
	struct dhmp_malloc_work *malloc_work;
	struct dhmp_msg msg;
	struct dhmp_mc_request req_msg;
	void *res_addr=NULL;

	malloc_work=(struct dhmp_malloc_work*)work->work_data;
	
	/*build malloc request msg*/
	req_msg.req_size=malloc_work->length;
	req_msg.addr_info=malloc_work->addr_info;
	req_msg.is_special=malloc_work->is_special;
	
	msg.msg_type=DHMP_MSG_MALLOC_REQUEST;
	msg.data_size=sizeof(struct dhmp_mc_request);
	msg.data=&req_msg;
	
	dhmp_post_send(malloc_work->rdma_trans, &msg);

	/*wait for the server return result*/
	while(malloc_work->addr_info->nvm_mr.length==0);
	if(malloc_work->is_special)
	goto end;	
	
	res_addr=malloc_work->addr_info->nvm_mr.addr;
	
	DEBUG_LOG ("get malloc addr %p", res_addr);
	
	if(res_addr==NULL)
		free(malloc_work->addr_info);
	else
	{
		res_addr=dhmp_transfer_dhmp_addr(malloc_work->rdma_trans,
										malloc_work->addr_info->nvm_mr.addr);
		malloc_work->addr_info->node_index=dhmp_get_node_index_from_addr(res_addr);
		malloc_work->addr_info->write_flag=false;
		dhmp_addr_info_insert_ht(res_addr, malloc_work->addr_info);
	}
	
	malloc_work->res_addr=res_addr;
end:
	malloc_work->done_flag=true;
}

void *dhmp_transfer_normal_addr(void *dhmp_addr)
{
	long long node_index, ll_addr;
	void *normal_addr;
	
	ll_addr=(long long)dhmp_addr;
	node_index=ll_addr>>48;

	node_index=node_index<<48;

	ll_addr-=node_index;
	normal_addr=(void*)ll_addr;

	return normal_addr;
}

/**
 *	dhmp_get_addr_info_from_ht:according to addr, find corresponding addr info
 */
struct dhmp_addr_info *dhmp_get_addr_info_from_ht(int index, void *dhmp_addr)
{
	struct dhmp_addr_info *addr_info;
	void *normal_addr;
	
	if(hlist_empty(&client->addr_info_ht[index]))
		goto out;
	else
	{
		normal_addr=dhmp_transfer_normal_addr(dhmp_addr);
		hlist_for_each_entry(addr_info, &client->addr_info_ht[index], addr_entry)
		{
			if(addr_info->nvm_mr.addr==normal_addr)
				break;
		}
	}
	
	if(!addr_info)
		goto out;
	
	return addr_info;
out:
	return NULL;
}


void dhmp_free_work_handler(struct dhmp_work *work)
{
	struct dhmp_msg msg;
	struct dhmp_addr_info *addr_info;
	struct dhmp_free_request req_msg;
	struct dhmp_free_work *free_work;
	int index;

	free_work=(struct dhmp_free_work*)work->work_data;

	/*get nvm mr from client hash table*/
	index=dhmp_hash_in_client(free_work->dhmp_addr);
	addr_info=dhmp_get_addr_info_from_ht(index, free_work->dhmp_addr);
	if(!addr_info)
	{
		ERROR_LOG("free addr error.");
		goto out;
	}
	hlist_del(&addr_info->addr_entry);
	
	/*build a msg of free addr*/
	req_msg.addr_info=addr_info;
	memcpy(&req_msg.mr, &addr_info->nvm_mr, sizeof(struct ibv_mr));
	
	msg.msg_type=DHMP_MSG_FREE_REQUEST;
	msg.data_size=sizeof(struct dhmp_free_request);
	msg.data=&req_msg;
	
	DEBUG_LOG("free addr is %p length is %d",
		addr_info->nvm_mr.addr, addr_info->nvm_mr.length);

	dhmp_post_send(free_work->rdma_trans, &msg);

	/*wait for the server return result*/
	while(addr_info->nvm_mr.addr!=NULL);

	free(addr_info);
out:
	free_work->done_flag=true;
}

void dhmp_read_work_handler(struct dhmp_work *work)
{
	struct dhmp_addr_info *addr_info;
	struct dhmp_rw_work *rwork;
	int index;
	struct timespec ts1,ts2;
	long long sleeptime;
	
	rwork=(struct dhmp_rw_work *)work->work_data;
	
	index=dhmp_hash_in_client(rwork->dhmp_addr);
	addr_info=dhmp_get_addr_info_from_ht(index, rwork->dhmp_addr);
	if(!addr_info)
	{
		ERROR_LOG("read addr is error.");
		goto out;
	}

	while(addr_info->write_flag);
	
	++addr_info->read_cnt;
		dhmp_rdma_read(rwork->rdma_trans, &addr_info->nvm_mr,
					rwork->local_addr, rwork->length);

out:
	rwork->done_flag=true;
}
	
void dhmp_write_work_handler(struct dhmp_work *work)
{
	struct dhmp_addr_info *addr_info;
	struct dhmp_rw_work *wwork;
	int index;
	struct timespec ts1,ts2;
	long long sleeptime;
	
	wwork=(struct dhmp_rw_work *)work->work_data;
	
	index=dhmp_hash_in_client(wwork->dhmp_addr);
	addr_info=dhmp_get_addr_info_from_ht(index, wwork->dhmp_addr);
	if(!addr_info)
	{
		ERROR_LOG("write addr is error.");
		goto out;
	}

	/*check no the same addr write task in qp*/
	while(addr_info->write_flag);
	addr_info->write_flag=true;
	
	++addr_info->write_cnt;
		dhmp_rdma_write(wwork->rdma_trans, addr_info, &addr_info->nvm_mr,
					wwork->local_addr, wwork->length);

out:
	wwork->done_flag=true;
}

void amper_clover_work_handler(struct dhmp_work *work)
{
	struct dhmp_addr_info *addr_info;
	struct amper_clover_work *wwork;
	int index;
	
	wwork=(struct amper_clover_work *)work->work_data;
	
	index=dhmp_hash_in_client(wwork->dhmp_addr);
	addr_info=dhmp_get_addr_info_from_ht(index, wwork->dhmp_addr);
	if(!addr_info)
	{
		ERROR_LOG("write addr is error.");
		goto out;
	}

	/*check no the same addr write task in qp*/
	while(addr_info->write_flag);
	addr_info->write_flag=true;
	++addr_info->write_cnt;

	struct dhmp_task* clover_task;
	struct ibv_send_wr send_wr,*bad_wr=NULL;
	struct ibv_sge sge;
	int err=0;
	struct ibv_mr* mr = &(addr_info->nvm_mr);

	clover_task = dhmp_write_task_create(wwork->rdma_trans, NULL, wwork->length);
	if(!L5_task)
	{
		ERROR_LOG("allocate memory error.");
		return -1;
	}
	
	memset(&send_wr, 0, sizeof(struct ibv_send_wr));
	send_wr.wr_id= ( uintptr_t ) clover_task;
	send_wr.opcode=IBV_WR_ATOMIC_CMP_AND_SWP;
	send_wr.sg_list=NULL;
	send_wr.num_sge=0;
	send_wr.send_flags=IBV_SEND_SIGNALED;
	send_wr.wr.atomic.remote_addr= ( uintptr_t ) mr->addr;
	send_wr.wr.atomic.rkey=mr->rkey;
	send_wr.wr.atomic.compare_add = 0ULL;
	send_wr.wr.atomic.swap = 0ULL;

	err=ibv_post_send ( wwork->rdma_trans->qp, &send_wr, &bad_wr );
	if ( err )
	{
		ERROR_LOG("ibv_post_send error");
		exit(-1);
		return -1;
	}	
	while(!clover_task->done_flag);
out:
	wwork->done_flag=true;
}

void amper_L5_work_handler(struct dhmp_work *work)   /// same as two write
{
	struct amper_L5_work *wwork;
	wwork=(struct amper_L5_work *)work->work_data;
	/*check no the same addr write task in qp*/
	struct dhmp_task* L5_task;
	struct ibv_send_wr send_wr,*bad_wr=NULL;
	struct ibv_sge sge;
	struct dhmp_send_mr* temp_mr=NULL;
	int err=0;
	
	memcpy(client->per_ops_mr_addr, wwork->local_addr, wwork->length);
	temp_mr=client->per_ops_mr;
	L5_task=dhmp_write_task_create(wwork->rdma_trans, temp_mr, wwork->length);
	if(!L5_task)
	{
		ERROR_LOG("allocate memory error.");
		return -1;
	}
	
	memset(&send_wr, 0, sizeof(struct ibv_send_wr));

	send_wr.wr_id= ( uintptr_t ) L5_task;
	send_wr.opcode=IBV_WR_RDMA_WRITE;
	send_wr.sg_list=&sge;
	send_wr.num_sge=1;
	send_wr.send_flags=IBV_SEND_SIGNALED;
	send_wr.wr.rdma.remote_addr= ( uintptr_t ) mr->addr;
	send_wr.wr.rdma.rkey=mr->rkey;

// 	sge.addr= ( uintptr_t ) write_task->sge.addr;
// 	sge.length=write_task->sge.length;
// 	sge.lkey=write_task->sge.lkey;
// #ifdef FLUSH
// 	struct dhmp_task* read_task;
// 	struct ibv_send_wr send_wr2,*bad_wr2=NULL;
// 	struct ibv_sge sge2;
// 	read_task=dhmp_read_task_create(rdma_trans, client->per_ops_mr2, length);
// 	if ( !read_task )
// 	{
// 		ERROR_LOG ( "allocate memory error." );
// 		return -1;
// 	}

// 	memset(&send_wr2, 0, sizeof(struct ibv_send_wr));

// 	send_wr2.wr_id= ( uintptr_t ) read_task;
// 	send_wr2.opcode=IBV_WR_RDMA_READ;
// 	send_wr2.sg_list=&sge2;
// 	send_wr2.num_sge=1; // or 1
// 	send_wr2.send_flags=IBV_SEND_SIGNALED;
// 	send_wr2.wr.rdma.remote_addr=(uintptr_t)mr->addr;
// 	send_wr2.wr.rdma.rkey=mr->rkey;

// 	sge2.addr=(uintptr_t)read_task->sge.addr;
// 	sge2.length=read_task->sge.length; 
// 	sge2.lkey=read_task->sge.lkey;
// #endif
// 	err=ibv_post_send ( rdma_trans->qp, &send_wr, &bad_wr );
// 	if ( err )
// 	{
// 		ERROR_LOG("ibv_post_send error");
// 		exit(-1);
// 		return -1;
// 	}		
// #ifdef FLUSH
// 	err=ibv_post_send(rdma_trans->qp, &send_wr2, &bad_wr2);
// 	if(err)
// 	{
// 		ERROR_LOG("ibv_post_send error");
// 		return -1;
// 	}

// 	DEBUG_LOG("before read_test_mr addr is %s", client->per_ops_mr2->mr->addr);
// 	while(!read_task->done_flag);			
// 	DEBUG_LOG("read_test_mr addr content is %s", client->per_ops_mr2->mr->addr);
// #else
// 	while(!write_task->done_flag);
// #endif


// out:
// 	wwork->done_flag=true;
}

void dhmp_write2_work_handler(struct dhmp_work *work)
{
	struct dhmp_rw2_work *wwork;
	struct dhmp_Write2_request req_msg;
	void * temp;

	wwork=(struct dhmp_rw2_work*)work->work_data;

	/*build malloc request msg*/
	req_msg.is_new = true;
	req_msg.req_size= wwork->length;
	req_msg.dhmp_addr = wwork->dhmp_addr;
	req_msg.task = wwork;
	
	dhmp_post_write2(wwork->rdma_trans, &req_msg, wwork->S_ringMR, wwork->local_addr);

	/*wait for the server return result*/
	while(!wwork->recv_flag);
	wwork->done_flag=true;
}

void dhmp_send_work_handler(struct dhmp_work *work)
{
	struct dhmp_Send_work *Send_work;
	struct dhmp_msg msg ;
	struct dhmp_Send_request req_msg;
	void * temp;

	Send_work=(struct dhmp_Send_work*)work->work_data;

	/*build malloc request msg*/
	req_msg.req_size=Send_work->length;
	req_msg.dhmp_addr = Send_work->dhmp_addr;
	req_msg.task = Send_work;
	req_msg.is_write = Send_work->is_write;
	req_msg.local_addr = Send_work->local_addr;

	msg.msg_type=DHMP_MSG_SEND_REQUEST;
	

		msg.data_size = sizeof(struct dhmp_Send_request) + Send_work->length;
		temp = malloc(msg.data_size );
		memcpy(temp,&req_msg,sizeof(struct dhmp_Send_request));
		memcpy(temp+sizeof(struct dhmp_Send_request),Send_work->local_addr, Send_work->length);
		msg.data = temp;

	dhmp_post_send(Send_work->rdma_trans, &msg);

	/*wait for the server return result*/
	while(!Send_work->recv_flag);
	Send_work->done_flag=true;
}

void dhmp_send2_work_handler(struct dhmp_work *work)
{
	struct dhmp_addr_info *addr_info;
	struct dhmp_Send2_work *wwork;
	int index;
	
	wwork=(struct dhmp_Send2_work *)work->work_data;
	
	index=dhmp_hash_in_client(wwork->dhmp_addr);
	addr_info=dhmp_get_addr_info_from_ht(index, wwork->dhmp_addr);
	if(!addr_info)
	{
		ERROR_LOG("write addr is error.");
		goto out;
	}

	/*check no the same addr write task in qp*/
	while(addr_info->write_flag);
	addr_info->write_flag=true;
	
	++addr_info->write_cnt;
	dhmp_rdma_write2(wwork->rdma_trans, addr_info, &addr_info->nvm_mr,
					wwork->local_addr, wwork->length);

	struct dhmp_msg msg ;
	struct dhmp_Send2_request req_msg;

	/*build malloc request msg*/
	req_msg.req_size=wwork->length;
	req_msg.server_addr = wwork->server_addr;
	req_msg.task = wwork;

	msg.msg_type=DHMP_MSG_SEND2_REQUEST;
	
	msg.data_size = sizeof(struct dhmp_Send2_request) ;
	msg.data = &req_msg;

	dhmp_post_send(wwork->rdma_trans, &msg);

	/*wait for the server return result*/
	while(!wwork->recv_flag);
out:
	wwork->done_flag=true;
}

void dhmp_ReqAddr1_work_handler(struct dhmp_work *dwork)
{
	struct reqAddr_work *work;
	struct dhmp_msg msg ;
	struct dhmp_ReqAddr1_request req_msg;

	work=(struct reqAddr_work*)dwork->work_data;

	/*build malloc request msg*/
	req_msg.req_size= work->length;
	req_msg.dhmp_addr = work->dhmp_addr;
	req_msg.task = work;
	req_msg.cmpflag = work->cmpflag;

	msg.msg_type=DHMP_MSG_REQADDR1_REQUEST;  
	
	msg.data_size = sizeof(struct dhmp_ReqAddr1_request);
	msg.data = &req_msg;

	dhmp_post_send(work->rdma_trans, &msg);

	/*wait for the server return result*/
	while(!work->recv_flag);
	work->done_flag=true;
}

void dhmp_WriteImm_work_handler(struct dhmp_work *work)
{
	struct dhmp_addr_info *addr_info;
	struct dhmp_writeImm_work *wwork;
	int index;
	
	wwork=(struct dhmp_writeImm_work *)work->work_data;
	
	index=dhmp_hash_in_client(wwork->dhmp_addr);
	addr_info=dhmp_get_addr_info_from_ht(index, wwork->dhmp_addr);
	if(!addr_info)
	{
		ERROR_LOG("write addr is error.");
		goto out;
	}

	/*check no the same addr write task in qp*/
	while(addr_info->write_flag);
	addr_info->write_flag=true;
	
	++addr_info->write_cnt;
	dhmp_post_writeImm(wwork->rdma_trans, addr_info, &addr_info->nvm_mr,
					wwork->local_addr, wwork->length, wwork->task_offset);

out:
	wwork->done_flag=true;
}

void dhmp_WriteImm2_work_handler(struct dhmp_work *work)
{
	struct dhmp_addr_info *addr_info;
	struct dhmp_writeImm2_work *wwork;
	int index;
	
	wwork=(struct dhmp_writeImm2_work *)work->work_data;
	
	index=dhmp_hash_in_client(wwork->dhmp_addr);
	addr_info=dhmp_get_addr_info_from_ht(index, wwork->dhmp_addr);
	if(!addr_info)
	{
		ERROR_LOG("write addr is error.");
		goto out;
	}

	/*check no the same addr write task in qp*/
	while(addr_info->write_flag);
	addr_info->write_flag=true;
	
	++addr_info->write_cnt;
	dhmp_rdma_write2(wwork->rdma_trans, addr_info, &addr_info->nvm_mr,
					wwork->local_addr, wwork->length);

	struct dhmp_WriteImm2_request req_msg;
	void * temp;

	/*build malloc request msg*/
	req_msg.msg_type=DHMP_MSG_WriteImm2_REQUEST;
	req_msg.req_size= wwork->length;
	req_msg.server_addr = wwork->server_addr;
	req_msg.task = wwork;
	
	dhmp_post_writeImm2(wwork->rdma_trans, &req_msg, wwork->S_ringMR, wwork->local_addr);

	/*wait for the server return result*/
	while(!wwork->recv_flag);
out:	
	wwork->done_flag=true;
}

void dhmp_WriteImm3_work_handler(struct dhmp_work *dwork)
{

	struct dhmp_writeImm3_work *work;
	struct dhmp_WriteImm3_request req_msg;
	void * temp;

	work=(struct dhmp_writeImm3_work*)dwork->work_data;

	/*build malloc request msg*/
	req_msg.msg_type=DHMP_MSG_WriteImm3_REQUEST;
	req_msg.req_size= work->length;
	req_msg.dhmp_addr = work->dhmp_addr;
	req_msg.task = work;
	
 dhmp_post_writeImm3(work->rdma_trans, &req_msg, work->S_ringMR, work->local_addr);


	/*wait for the server return result*/
	while(!work->recv_flag);

	work->done_flag=true;
}

void dhmp_sread_work_handler(struct dhmp_work *work)
{
	struct dhmp_Sread_work *wwork;
	struct dhmp_msg msg ;
	struct dhmp_Sread_request req_msg;
	void * temp;

	wwork=(struct dhmp_Sread_work*)work->work_data;

	struct dhmp_addr_info *addr_info;
	int index = dhmp_hash_in_client(wwork->dhmp_addr);
	addr_info=dhmp_get_addr_info_from_ht(index, wwork->dhmp_addr);

	/*build malloc request msg*/
	req_msg.req_size=wwork->length;
	
	req_msg.task = wwork;
	
	memcpy(client->per_ops_mr_addr, wwork->local_addr , wwork->length);
	struct ibv_mr * temp_mr=client->per_ops_mr->mr;
	memcpy(&(req_msg.client_mr) , temp_mr , sizeof(struct ibv_mr));
	memcpy(&(req_msg.server_mr)  , &(addr_info->nvm_mr), sizeof(struct ibv_mr));

	msg.msg_type=DHMP_MSG_Sread_REQUEST;
	msg.data_size = sizeof(struct dhmp_Sread_request);
	msg.data = &req_msg;

	dhmp_post_send(wwork->rdma_trans, &msg);

	/*wait for the server return result*/
	while(!wwork->recv_flag);
error:
	wwork->done_flag=true;
}

void dhmp_close_work_handler(struct dhmp_work *work)
{
	struct dhmp_close_work *cwork;
	struct dhmp_msg msg;
	int tmp=0;
	
	cwork=(struct dhmp_close_work*)work->work_data;

	msg.msg_type=DHMP_MSG_CLOSE_CONNECTION;
	msg.data_size=sizeof(int);
	msg.data=&tmp;
	
	dhmp_post_send(cwork->rdma_trans, &msg);

	cwork->done_flag=true;
}

void *dhmp_work_handle_thread(void *data)
{
	struct dhmp_work *work;

	while(1)
	{
		work=NULL;
		
		pthread_mutex_lock(&client->mutex_work_list);
		if(!list_empty(&client->work_list))
		{
			work=list_first_entry(&client->work_list, struct dhmp_work, work_entry);
			list_del(&work->work_entry);
		}
		pthread_mutex_unlock(&client->mutex_work_list);

		if(work)
		{
			switch (work->work_type)
			{
				case DHMP_WORK_MALLOC:
					dhmp_malloc_work_handler(work);
					break;
				case DHMP_WORK_FREE:
					dhmp_free_work_handler(work);
					break;
				case DHMP_WORK_READ:
					dhmp_read_work_handler(work);
					break;
				case AMPER_WORK_CLOVER:
					amper_clover_work_handler(work);
					break;
				case AMPER_WORK_L5:
					amper_L5_work_handler(work);
					break;
				case DHMP_WORK_WRITE:
					dhmp_write_work_handler(work);
					break;
				case DHMP_WORK_WRITE2:
					dhmp_write2_work_handler(work);
					break;
				case DHMP_WORK_SEND:
					dhmp_send_work_handler(work);
					break;
				case DHMP_WORK_SEND2:
					dhmp_send2_work_handler(work);
					break;
				case DHMP_WORK_SREAD:
					dhmp_sread_work_handler(work);
					break;
				case DHMP_WORK_REQADDR1:
					dhmp_ReqAddr1_work_handler(work);
					break;
				case DHMP_WORK_WRITEIMM:
					dhmp_WriteImm_work_handler(work);
					break;
				case DHMP_WORK_WRITEIMM2:
					dhmp_WriteImm2_work_handler(work);
					break;
				case DHMP_WORK_WRITEIMM3:
					dhmp_WriteImm3_work_handler(work);
					break;
				case DHMP_WORK_CLOSE:
					dhmp_close_work_handler(work);
					break;
				default:
					ERROR_LOG("work exist error.");
					break;
			}
		}
		
	}

	return NULL;
}



struct dhmp_server_addr_info *dhmp_get_addr_info_from_ht_at_Server(int index, void *dhmp_addr)
{
	struct dhmp_server_addr_info *addr_info;
	void *normal_addr;
	
	if(hlist_empty(&server->addr_info_ht[index]))
		goto out;
	else
	{
		normal_addr=dhmp_transfer_normal_addr(dhmp_addr);
		hlist_for_each_entry(addr_info, &server->addr_info_ht[index], addr_entry)
		{
			if(addr_info->server_addr == normal_addr)
				break;
		}
	}
	
	if(!addr_info)
		goto out;
INFO_LOG("find server hash %p",normal_addr);	
	return addr_info;
out:
	return NULL;
}
