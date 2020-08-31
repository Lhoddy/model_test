#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <x86intrin.h> 

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

#define NUM_writeImm3 ((uint32_t)-3)
#define NUM_writeImm2 ((uint32_t)-2)

/*declare static function in here*/
static void dhmp_post_recv(struct dhmp_transport* rdma_trans, void *addr);
static void dhmp_post_all_recv(struct dhmp_transport *rdma_trans);
bool dhmp_destroy_dram_entry(void *nvm_addr);

void * nvm_malloc(size_t size)
{
	return malloc(size);
}

void nvm_free(void *addr)
{
	return free(addr);
}

/**
 *	return the work completion operation code string.
 */
const char* dhmp_wc_opcode_str(enum ibv_wc_opcode opcode)
{
	switch(opcode)
	{
		case IBV_WC_SEND:
			return "IBV_WC_SEND";
		case IBV_WC_RDMA_WRITE:
			return "IBV_WC_RDMA_WRITE";
		case IBV_WC_RDMA_READ:
			return "IBV_WC_RDMA_READ";
		case IBV_WC_COMP_SWAP:
			return "IBV_WC_COMP_SWAP";
		case IBV_WC_FETCH_ADD:
			return "IBV_WC_FETCH_ADD";
		case IBV_WC_BIND_MW:
			return "IBV_WC_BIND_MW";
		case IBV_WC_RECV:
			return "IBV_WC_RECV";
		case IBV_WC_RECV_RDMA_WITH_IMM:
			return "IBV_WC_RECV_RDMA_WITH_IMM";
		default:
			return "IBV_WC_UNKNOWN";
	};
}

/**
 *	below functions about malloc memory in dhmp_server
 */
static struct dhmp_free_block* dhmp_get_free_block(struct dhmp_area* area,
														int index)
{
	struct dhmp_free_block *res,*left_blk,*right_blk;
	int i;

	res=left_blk=right_blk=NULL;
retry:
	for(i=index; i<MAX_ORDER; i++)
	{
		if(!list_empty(&area->block_head[i].free_block_list))
			break;
	}

	if(i==MAX_ORDER)
	{
		ERROR_LOG("don't exist enough large free block.");
		res=NULL;
		goto out;
	}

	if(i!=index)
	{
		right_blk=malloc(sizeof(struct dhmp_free_block));
		if(!right_blk)
		{
			ERROR_LOG("allocate memory error.");
			goto out;
		}

		left_blk=list_entry(area->block_head[i].free_block_list.next,
						struct dhmp_free_block, free_block_entry);
		list_del(&left_blk->free_block_entry);
		left_blk->size=left_blk->size/2;

		right_blk->mr=left_blk->mr;
		right_blk->size=left_blk->size;
		right_blk->addr=left_blk->addr+left_blk->size;

		list_add(&right_blk->free_block_entry, 
				&area->block_head[i-1].free_block_list);
		list_add(&left_blk->free_block_entry, 
				&area->block_head[i-1].free_block_list);
		
		area->block_head[i].nr_free-=1;
		area->block_head[i-1].nr_free+=2;
		goto retry;
	}
	else
	{
		res=list_entry(area->block_head[i].free_block_list.next,
						struct dhmp_free_block, free_block_entry);
		list_del(&res->free_block_entry);
		area->block_head[i].nr_free-=1;
	}

	for(i=area->max_index; i>=0; i--)
	{
		if(!list_empty(&area->block_head[i].free_block_list))
			break;
	}

	area->max_index=i;
	
out:
	return res;
}

static bool dhmp_malloc_one_block(struct dhmp_msg* msg,
										struct dhmp_mc_response* response)
{
	struct dhmp_free_block* free_blk=NULL;
	struct dhmp_area* area=NULL;
	int index=0;

	for(index=0; index<MAX_ORDER; index++)
		if(response->req_info.req_size<=buddy_size[index])
			break;
	
	if(server->cur_area->max_index>=index)
		area=server->cur_area;
	else
	{
		list_for_each_entry(area, &server->area_list, area_entry)
		{
			if(area->max_index>=index)
			{
				server->cur_area=area;
				break;
			}
		}
	}
	
	if(&area->area_entry == &(server->area_list))
	{
		area=dhmp_area_create(true, SINGLE_AREA_SIZE);
		if(!area)
		{
			ERROR_LOG ( "allocate area memory error." );
			goto out;
		}
		server->cur_area=area;
	}
	
	free_blk=dhmp_get_free_block(area, index);
	if(!free_blk)
	{
		ERROR_LOG("fetch free block error.");
		goto out;
	}

	memcpy(&response->mr, free_blk->mr, sizeof(struct ibv_mr));
	response->mr.addr=free_blk->addr;
	response->mr.length=free_blk->size;
	response->server_addr = free_blk->addr;

	DEBUG_LOG("malloc addr %p lkey %ld length is %d",
			free_blk->addr, free_blk->mr->lkey, free_blk->mr->length );

	snprintf(free_blk->addr, response->req_info.req_size, "nvmhhhhhhhh%p", response->server_addr);

	free(free_blk);
	return true;

out:
	return false;
}


static bool dhmp_malloc_more_area(struct dhmp_msg* msg, 
										struct dhmp_mc_response* response_msg,
										size_t length )
{
	struct dhmp_area *area;

	area=dhmp_area_create(false, length);
	if(!area)
	{
		ERROR_LOG("allocate one area error.");
		return false;
	}
	
	memcpy(&response_msg->mr, area->mr, sizeof(struct ibv_mr));
	response_msg->server_addr = area->server_addr;

	INFO_LOG("malloc addr %p lkey %ld",
			response_msg->mr.addr, response_msg->mr.lkey);
	
	snprintf(response_msg->mr.addr,
			response_msg->req_info.req_size, 
			"welcomebj%p", area);

	return true;
}


static void dhmp_send_request_handler(struct dhmp_transport* rdma_trans,
												struct dhmp_msg* msg)
{
	struct timespec task_time_start, task_time_end;
	unsigned long task_time_diff_ns;
	clock_gettime(CLOCK_MONOTONIC, &task_time_start);

	struct dhmp_Send_response response;
	struct dhmp_msg res_msg;
	void * server_addr = NULL;
	void * temp;

	memcpy ( &response.req_info, msg->data, sizeof(struct dhmp_Send_request));
	size_t length = response.req_info.req_size;
	INFO_LOG ( "client operate size %d",length);

	/*get server addr from dhmp_addr*/
	void * dhmp_addr = response.req_info.dhmp_addr;
	server_addr = dhmp_addr;

	res_msg.msg_type=DHMP_MSG_SEND_RESPONSE;
	
	if(response.req_info.is_write == true) 
	{
		res_msg.data_size=sizeof(struct dhmp_Send_response);
		res_msg.data=&response;
		/*may be need mutex*/
		memcpy(server_addr , (msg->data+sizeof(struct dhmp_Send_request)),length);
		_mm_clflush(server_addr);
	}
	else
	{
		res_msg.data_size = sizeof(struct dhmp_Send_response) + length;
		temp = malloc(res_msg.data_size );
		memcpy(temp,&response,sizeof(struct dhmp_Send_response));
		memcpy(temp+sizeof(struct dhmp_Send_response),server_addr, length);
		res_msg.data = temp;
	}

	dhmp_post_send(rdma_trans, &res_msg);
	clock_gettime(CLOCK_MONOTONIC, &task_time_end);
	task_time_diff_ns = ((task_time_end.tv_sec * 1000000000) + task_time_end.tv_nsec) -
                        ((task_time_start.tv_sec * 1000000000) + task_time_start.tv_nsec);
  	printf("runtime %lf\n", (double)task_time_diff_ns/1000000);
  	clock_gettime(CLOCK_MONOTONIC, &task_time_start);
	return ;

}

static void dhmp_send_response_handler(struct dhmp_transport* rdma_trans,struct dhmp_msg* msg)
{
	struct dhmp_Send_response response_msg;
	struct dhmp_server_addr_info *addr_info;
	memcpy(&response_msg, msg->data, sizeof(struct dhmp_Send_response));

	if(! response_msg.req_info.is_write)
	{
		memcpy(response_msg.req_info.local_addr ,
			 (msg->data+sizeof(struct dhmp_Send_response)),response_msg.req_info.req_size);
}

	struct dhmp_Send_work * task = response_msg.req_info.task;
	task->recv_flag = true;
	DEBUG_LOG("response send addr %p ",response_msg.req_info.dhmp_addr);
}


static void dhmp_malloc_request_handler(struct dhmp_transport* rdma_trans,
												struct dhmp_msg* msg)
{
	struct dhmp_mc_response response;
	struct dhmp_msg res_msg;
	bool res=true;
	
	memcpy ( &response.req_info, msg->data, sizeof(struct dhmp_mc_request));
	INFO_LOG ( "client req size %d",response.req_info.req_size);

	if(response.req_info.is_special == 1)
	{
		struct ibv_mr * mr = dhmp_malloc_poll_area(SINGLE_POLL_RECV_REGION);
		if(mr == NULL)
			goto req_error;
		memcpy(&(response.mr), mr,sizeof(struct ibv_mr));
		response.server_addr = mr->addr;
		server->rdma_trans = rdma_trans;	
	}
	else if(response.req_info.is_special == 2) //for clover
	{
		struct ibv_mr * mr = dhmp_malloc_poll_area(response.req_info.req_size);//obj_num, for clover c&s
		if(mr == NULL)
			goto req_error;
		memcpy(&(response.mr), mr,sizeof(struct ibv_mr));
		response.server_addr = mr->addr;
		server->rdma_trans = rdma_trans;	
	}
	else if(response.req_info.is_special == 3)// for L5
	{
		memcpy(&(response.mr), server->L5_mailbox.mr, sizeof(struct ibv_mr));
		response.mr.addr += rdma_trans->node_id;
		memcpy(&(response.mr2), server->L5_message[rdma_trans->node_id].mr, sizeof(struct ibv_mr));
		memcpy(&(response.read_mr), server->L5_mailbox.read_mr, sizeof(struct ibv_mr));
	}
	else if(response.req_info.is_special == 4)// for tailwind
	{
		amper_allocspace_for_server(rdma_trans, 4, response.req_info.req_size); 
		memcpy(&(response.mr), server->Tailwind_buffer[rdma_trans->node_id].mr, sizeof(struct ibv_mr));
		memcpy(&(response.read_mr), server->Tailwind_buffer[rdma_trans->node_id].read_mr, sizeof(struct ibv_mr));
	}
	else if(response.req_info.is_special == 5) // for DaRPC
	{
		memcpy(&(response.read_mr), server->read_mr, sizeof(struct ibv_mr));
	}
	else
	{
		if(response.req_info.req_size <= buddy_size[MAX_ORDER-1])
		{
			res=dhmp_malloc_one_block(msg, &response);
		}
		else if(response.req_info.req_size <= SINGLE_AREA_SIZE)
		{
			res=dhmp_malloc_more_area(msg, &response, SINGLE_AREA_SIZE);
		}
		else
		{
			res=dhmp_malloc_more_area(msg, &response, response.req_info.req_size);
		}
		if(!res)
			goto req_error;
	}
	
	res_msg.msg_type=DHMP_MSG_MALLOC_RESPONSE;
	res_msg.data_size=sizeof(struct dhmp_mc_response);
	res_msg.data=&response;
	dhmp_post_send(rdma_trans, &res_msg);

	rdma_trans->nvm_used_size+=response.mr.length;
	return ;

req_error:
	/*transmit a message of DHMP_MSG_MALLOC_ERROR*/
	res_msg.msg_type=DHMP_MSG_MALLOC_ERROR;
	res_msg.data_size=sizeof(struct dhmp_mc_response);
	res_msg.data=&response;

	dhmp_post_send ( rdma_trans, &res_msg );

	return ;
}

static void dhmp_malloc_response_handler(struct dhmp_transport* rdma_trans,
													struct dhmp_msg* msg)
{
	struct dhmp_mc_response response_msg;
	struct dhmp_addr_info *addr_info;

	memcpy(&response_msg, msg->data, sizeof(struct dhmp_mc_response));
	if(response_msg.req_info.is_special == 1)
	{
		memcpy(&(client->ringbuffer.mr), &response_msg.mr, sizeof(struct ibv_mr)); 
		client->ringbuffer.length = SINGLE_POLL_RECV_REGION;
		client->ringbuffer.cur = 0;
		DEBUG_LOG("responde poll region size = %ld",SINGLE_POLL_RECV_REGION);
	}
	else if(response_msg.req_info.is_special == 2)
	{
		memcpy(&(client->clover.mr), &response_msg.mr, sizeof(struct ibv_mr)); 
		// client->clover_point.length = response_msg.req_info.req_size;
		DEBUG_LOG("responde clover region size = %ld",response_msg.req_info.req_size);// assume = obj_num
	}
	else if(response_msg.req_info.is_special == 3)
	{
		memcpy(&(client->L5.mailbox_mr), &response_msg.mr, sizeof(struct ibv_mr));
		memcpy(&(client->L5.message_mr), &response_msg.mr2, sizeof(struct ibv_mr)); 
		memcpy(&(client->L5.read_mr), &response_msg.read_mr, sizeof(struct ibv_mr)); 
	}
	else if(response_msg.req_info.is_special == 4)
	{
		memcpy(&(client->Tailwind_buffer.mr), &response_msg.mr, sizeof(struct ibv_mr)); 
		memcpy(&(client->Tailwind_buffer.read_mr), &response_msg.read_mr, sizeof(struct ibv_mr)); 
	}
	else if(response_msg.req_info.is_special == 5)
	{
		memcpy(&(client->read_mr), &response_msg.read_mr, sizeof(struct ibv_mr)); 
	}
	else{
		addr_info=response_msg.req_info.addr_info;
		memcpy(&addr_info->nvm_mr, &response_msg.mr, sizeof(struct ibv_mr));   
		DEBUG_LOG("response mr addr %p lkey %ld",
				addr_info->nvm_mr.addr, addr_info->nvm_mr.lkey);
	}
}

static void dhmp_malloc_error_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg)
{
	struct dhmp_mc_response response_msg;
	struct dhmp_addr_info *addr_info;
	
	memcpy( &response_msg, msg->data, sizeof(struct dhmp_mc_response));
	addr_info=response_msg.req_info.addr_info;
	addr_info->nvm_mr.length=response_msg.req_info.req_size;
	addr_info->nvm_mr.addr=NULL;
}

/**
 *	below functions about free memory in dhmp_server
 */
static struct dhmp_free_block* dhmp_free_blk_create(void* addr,
															size_t size,
															struct ibv_mr* mr)
{
	struct dhmp_free_block* new_free_blk;

	new_free_blk=malloc(sizeof(struct dhmp_free_block));
	if(!new_free_blk)
	{
		ERROR_LOG ( "allocate memory error." );
		return NULL;
	}
	
	new_free_blk->addr=addr;
	new_free_blk->size=size;
	new_free_blk->mr=mr;
	return new_free_blk;
}

static bool dhmp_recycle_free_block(struct dhmp_area* area,
											void** addr_ptr, int index)
{
	bool left_dir=true;
	void* addr=*addr_ptr;
	struct list_head* free_list;
	struct dhmp_free_block *new_free_blk=NULL,*large_free_blk,*tmp;

	free_list=&area->block_head[index].free_block_list;
	if(list_empty(free_list))
	{
		new_free_blk=dhmp_free_blk_create(addr, buddy_size[index], area->mr);
		list_add(&new_free_blk->free_block_entry, free_list);
		return false;
	}

	/*can not merge up index+1*/
	if(index==MAX_ORDER-1)
	{
		list_for_each_entry(large_free_blk, free_list, free_block_entry)
		{
			if(addr<large_free_blk->addr)
				goto create_free_blk;
		}
		goto create_free_blk;
	}
	
	if((addr-area->mr->addr)%buddy_size[index+1]!=0)
		left_dir=false;

	list_for_each_entry(tmp, free_list, free_block_entry)
	{
		if((left_dir&&(addr+buddy_size[index]==tmp->addr))||
			(!left_dir&&(tmp->addr+buddy_size[index]==addr)))
		{
			list_del(&tmp->free_block_entry);
			*addr_ptr=min(addr, tmp->addr);
			return true;
		}
	}

	list_for_each_entry(large_free_blk, free_list, free_block_entry)
	{
		if(addr<large_free_blk->addr)
			break;
	}

create_free_blk:
	new_free_blk=dhmp_free_blk_create(addr, buddy_size[index], area->mr);
	list_add_tail(&new_free_blk->free_block_entry, &large_free_blk->free_block_entry);
	return false;
}

static void dhmp_free_one_block(struct ibv_mr* mr)
{
	struct dhmp_area* area;
	int i,index;
	bool res;
	struct dhmp_free_block* free_blk;
	
	DEBUG_LOG("free one block %p size %d", mr->addr, mr->length);

	list_for_each_entry(area, &server->area_list, area_entry)
	{
		if(mr->lkey==area->mr->lkey)
			break;
	}
	
	if((&area->area_entry) != (&server->area_list))
	{
		for(index=0; index<MAX_ORDER; index++)
			if(mr->length==buddy_size[index])
				break;
			
retry:
		res=dhmp_recycle_free_block(area, &mr->addr, index);
		if(res&&(index!=MAX_ORDER-1))
		{
			index+=1;
			goto retry;
		}
		
		for(i=MAX_ORDER-1;i>=0;i--)
		{
			if(!list_empty(&area->block_head[i].free_block_list))
			{
				area->max_index=i;
				break;
			}
		}
		
		for ( i=0; i<MAX_ORDER; i++ )
		{
			list_for_each_entry(free_blk,
							&area->block_head[i].free_block_list,
							free_block_entry)
			{
				DEBUG_LOG("Index %d addr %p",i,free_blk->addr);
			}
		}
		
	}
}

static void dhmp_free_one_area(struct ibv_mr* mr)
{
	struct dhmp_area* area;
	bool res;
	void *addr;
	
	DEBUG_LOG("free one area %p size %d",mr->addr,mr->length);
	
	list_for_each_entry(area, &server->more_area_list, area_entry)
	{
		if(mr->lkey==area->mr->lkey)
			break;
	}
	
	if((&area->area_entry) != (&server->area_list))
	{
		res=dhmp_buddy_system_build(area);
		if(res)
		{
			list_del(&area->area_entry);
			area->max_index=MAX_ORDER-1;
			list_add(&area->area_entry, &server->area_list);
		}
		else
		{
			list_del(&area->area_entry);
			addr=area->mr->addr;
			ibv_dereg_mr(area->mr);
			free(addr);
			free(area);
		}
	}
}

static void dhmp_free_more_area(struct ibv_mr* mr)
{
	struct dhmp_area* area;
	void *addr;
	
	DEBUG_LOG("free more area %p size %d", mr->addr, mr->length);
	
	list_for_each_entry(area, &server->more_area_list, area_entry)
	{
		if(mr->lkey==area->mr->lkey)
			break;
	}
	
	if((&area->area_entry) != (&server->area_list))
	{
		list_del(&area->area_entry);
		addr=area->mr->addr;
		ibv_dereg_mr(area->mr);
		free(addr);
		free(area);
	}
}

static void dhmp_free_request_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg_ptr)
{
	struct ibv_mr* mr;
	struct dhmp_free_request *request_msg_ptr;
	struct dhmp_free_response free_res_msg;
	struct dhmp_msg res_msg;
	
	request_msg_ptr=msg_ptr->data;
	mr=&request_msg_ptr->mr;

	rdma_trans->nvm_used_size-=mr->length;
	if(mr->length<=buddy_size[MAX_ORDER-1])
		dhmp_free_one_block(mr);
	else if(mr->length<=SINGLE_AREA_SIZE)
		dhmp_free_one_area(mr);
	else
		dhmp_free_more_area(mr);

	free_res_msg.addr_info=request_msg_ptr->addr_info;
	//free hash in server
	// void * dhmp_addr = request_msg_ptr->dhmp_addr;
	// int index=dhmp_hash_in_client(dhmp_addr);
	// struct dhmp_server_addr_info* addr_info=dhmp_get_addr_info_from_ht_at_Server(index, dhmp_addr);
	// if(!addr_info)
	// {
	// 	ERROR_LOG("free addr error.");
	// 	goto out;
	// }
	// hlist_del(&addr_info->addr_entry);
	//free hash in server
out:
	res_msg.msg_type=DHMP_MSG_FREE_RESPONSE;
	res_msg.data_size=sizeof(struct dhmp_free_response);
	res_msg.data=&free_res_msg;

	dhmp_post_send(rdma_trans, &res_msg);
	
}

static void dhmp_free_response_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg)
{
	struct dhmp_free_response *response_msg_ptr;
	struct dhmp_addr_info *addr_info;

	response_msg_ptr=msg->data;
	addr_info=response_msg_ptr->addr_info;
	addr_info->nvm_mr.addr=NULL;
}



/**
 *	dhmp_wc_recv_handler:handle the IBV_WC_RECV event
 */
static void dhmp_wc_recv_handler(struct dhmp_transport* rdma_trans,
										struct dhmp_msg* msg)
{
	switch(msg->msg_type)
	{
		case DHMP_MSG_MALLOC_REQUEST:
			dhmp_malloc_request_handler(rdma_trans, msg);
			break;
		case DHMP_MSG_MALLOC_RESPONSE:
			dhmp_malloc_response_handler(rdma_trans, msg);
			break;
		case DHMP_MSG_MALLOC_ERROR:
			dhmp_malloc_error_handler(rdma_trans, msg);
			break;
		case DHMP_MSG_FREE_REQUEST:
			dhmp_free_request_handler(rdma_trans, msg);
			break;
		case DHMP_MSG_FREE_RESPONSE:
			dhmp_free_response_handler(rdma_trans, msg);
			break;
		case DHMP_MSG_CLOSE_CONNECTION:
			rdma_disconnect(rdma_trans->cm_id);
			break;
		case DHMP_MSG_SEND_REQUEST:
		 	dhmp_send_request_handler(rdma_trans, msg);
		 	break;
		case DHMP_MSG_SEND_RESPONSE:
		 	dhmp_send_response_handler(rdma_trans, msg);
		 	break;
		case DHMP_MSG_SEND2_REQUEST:
		 	dhmp_send2_request_handler(rdma_trans, msg);
		 	break;
		case DHMP_MSG_SEND2_RESPONSE:
		 	dhmp_send2_response_handler(rdma_trans, msg);
		 	break;
		case DHMP_MSG_REQADDR1_REQUEST:
		 	dhmp_ReqAddr1_request_handler(rdma_trans, msg);
		 	break;
		case DHMP_MSG_REQADDR1_RESPONSE:
		 	dhmp_ReqAddr1_response_handler(rdma_trans, msg);
		 	break;
		case DHMP_MSG_WriteImm_RESPONSE:
		 	dhmp_WriteImm_response_handler(rdma_trans, msg);
		 	break;
		case DHMP_MSG_WriteImm2_RESPONSE:
		 	dhmp_WriteImm2_response_handler(rdma_trans, msg);
		 	break; 
		case DHMP_MSG_WriteImm3_RESPONSE:
		 	dhmp_WriteImm3_response_handler(rdma_trans, msg);
		 	break;
		case DHMP_MSG_Write2_RESPONSE:
		 	dhmp_Write2_response_handler(rdma_trans, msg);
		 	break; 
		case DHMP_MSG_Sread_REQUEST:
		 	dhmp_Sread_request_handler(rdma_trans, msg);
		 	break;
		case DHMP_MSG_Sread_RESPONSE:
		 	dhmp_Sread_response_handler(rdma_trans, msg);
		 	break;
		case DHMP_MSG_Tailwind_RPC_requeset:
			amper_tailwindRPC_request_handler(rdma_trans,msg);
			break;
		case DHMP_MSG_Tailwind_RPC_response:
			amper_tailwindRPC_response_handler(rdma_trans,msg);
			break;
		case DHMP_MSG_DaRPC_requeset:
			amper_DaRPC_request_handler(rdma_trans,msg);
			break;
		case DHMP_MSG_DaRPC_response:
			amper_DaRPC_response_handler(rdma_trans,msg);
			break;		
			
	}
}

/**
 *	the success work completion handler function
 */
static void dhmp_wc_success_handler(struct ibv_wc* wc)
{
	struct dhmp_task *task_ptr;
	struct dhmp_transport *rdma_trans;
	struct dhmp_msg msg;
	task_ptr=(struct dhmp_task*)(uintptr_t)wc->wr_id;
	rdma_trans=task_ptr->rdma_trans;
	if(wc->opcode == IBV_WC_RECV)
	{
		/*read the msg content from the task_ptr sge addr*/
		msg.msg_type=*(enum dhmp_msg_type*)task_ptr->sge.addr;
		msg.data_size=*(size_t*)(task_ptr->sge.addr+sizeof(enum dhmp_msg_type));
		msg.data=task_ptr->sge.addr+sizeof(enum dhmp_msg_type)+sizeof(size_t);
	}
	INFO_LOG("opcode:%s , %p",dhmp_wc_opcode_str(wc->opcode),rdma_trans);
	switch(wc->opcode)
	{
		case IBV_WC_RECV_RDMA_WITH_IMM:
			if(wc->imm_data == NUM_writeImm3)
				dhmp_WriteImm3_request_handler(rdma_trans);
			else if(wc->imm_data == NUM_writeImm2)
				dhmp_WriteImm2_request_handler(rdma_trans);
			else
			{
				dhmp_WriteImm_request_handler(wc->imm_data);
			}
			dhmp_post_recv(rdma_trans, task_ptr->sge.addr);
		case IBV_WC_SEND:
			break;
		case IBV_WC_RECV:
			dhmp_wc_recv_handler(rdma_trans, &msg);

			dhmp_post_recv(rdma_trans, task_ptr->sge.addr);
			break;
		case IBV_WC_RDMA_WRITE:
#ifdef model_B
#else
			if(task_ptr->addr_info != NULL)
				task_ptr->addr_info->write_flag=false;
#endif
			task_ptr->done_flag=true;
			break;
		case IBV_WC_RDMA_READ:
			task_ptr->done_flag=true;
			break;
		default:
			ERROR_LOG("unknown opcode:%s",
			            dhmp_wc_opcode_str(wc->opcode));
			break;
	}
}

/**
 *	dhmp_wc_error_handler:handle the error work completion.
 */
static void dhmp_wc_error_handler(struct ibv_wc* wc)
{
	if(wc->status==IBV_WC_WR_FLUSH_ERR)
		INFO_LOG("work request flush");
	else
		ERROR_LOG("wc status is [%s]",
		            ibv_wc_status_str(wc->status));
}

/**
 *	dhmp_comp_channel_handler:create a completion channel handler
 *  note:set the following function to the cq handle work completion
 */
static void dhmp_comp_channel_handler(int fd, void* data)
{
	struct dhmp_cq* dcq =(struct dhmp_cq*) data;
	struct ibv_cq* cq;
	void* cq_ctx;
	struct ibv_wc wc;
	int err=0;

	err=ibv_get_cq_event(dcq->comp_channel, &cq, &cq_ctx);
	if(err)
	{
		ERROR_LOG("ibv get cq event error.");
		return ;
	}

	ibv_ack_cq_events(dcq->cq, 1);
	err=ibv_req_notify_cq(dcq->cq, 0);
	if(err)
	{
		ERROR_LOG("ibv req notify cq error.");
		return ;
	}

	while(ibv_poll_cq(dcq->cq, 1, &wc))
	{
		if(wc.status==IBV_WC_SUCCESS)
			dhmp_wc_success_handler(&wc);
		else
			dhmp_wc_error_handler(&wc);
	}
}

/*
 *	get the cq because send queue and receive queue need to link it
 */

static struct dhmp_cq* dhmp_cq_get(struct dhmp_device* device, struct dhmp_context* ctx)
{
	struct dhmp_cq* dcq;
	int retval,flags=0;

	dcq=(struct dhmp_cq*) calloc(1,sizeof(struct dhmp_cq));
	if(!dcq)
	{
		ERROR_LOG("allocate the memory of struct dhmp_cq error.");
		return NULL;
	}

	dcq->comp_channel=ibv_create_comp_channel(device->verbs);
	if(!dcq->comp_channel)
	{
		ERROR_LOG("rdma device %p create comp channel error.", device);
		goto cleanhcq;
	}

	flags=fcntl(dcq->comp_channel->fd, F_GETFL, 0);
	if(flags!=-1)
		flags=fcntl(dcq->comp_channel->fd, F_SETFL, flags|O_NONBLOCK);

	if(flags==-1)
	{
		ERROR_LOG("set hcq comp channel fd nonblock error.");
		goto cleanchannel;
	}

	dcq->ctx=ctx;
	retval=dhmp_context_add_event_fd(dcq->ctx,
									EPOLLIN,
									dcq->comp_channel->fd,
									dcq, dhmp_comp_channel_handler);
	if(retval)
	{
		ERROR_LOG("context add comp channel fd error.");
		goto cleanchannel;
	}

	dcq->cq=ibv_create_cq(device->verbs, 100000, dcq, dcq->comp_channel, 0);
	if(!dcq->cq)
	{
		ERROR_LOG("ibv create cq error.");
		goto cleaneventfd;
	}

	retval=ibv_req_notify_cq(dcq->cq, 0);
	if(retval)
	{
		ERROR_LOG("ibv req notify cq error.");
		goto cleaneventfd;
	}

	dcq->device=device;
	return dcq;

cleaneventfd:
	dhmp_context_del_event_fd(ctx, dcq->comp_channel->fd);

cleanchannel:
	ibv_destroy_comp_channel(dcq->comp_channel);

cleanhcq:
	free(dcq);

	return NULL;
}

/*
 *	create the qp resource for the RDMA connection
 */
static int dhmp_qp_create(struct dhmp_transport* rdma_trans)
{
	int retval=0;
	struct ibv_qp_init_attr qp_init_attr;
	struct dhmp_cq* dcq;
#ifdef DaRPC_SERVER
	int index = rdma_trans->node_id / DaRPC_clust_NUM;
	
	if( server->DaRPC_dcq[index] == NULL)
	{
		server->DaRPC_dcq[index] = dhmp_cq_get(rdma_trans->device, rdma_trans->ctx); 
		if(!server->DaRPC_dcq[index])
		{
			ERROR_LOG("dhmp cq get error.");
			return -1;
		}
	}
	dcp = server->DaRPC_dcq[index];
#else
	dcq=dhmp_cq_get(rdma_trans->device, rdma_trans->ctx);
	if(!dcq)
	{
		ERROR_LOG("dhmp cq get error.");
		return -1;
	}
#endif
	memset(&qp_init_attr,0,sizeof(qp_init_attr));
	qp_init_attr.qp_context=rdma_trans;
	qp_init_attr.qp_type=IBV_QPT_RC;
	qp_init_attr.send_cq=dcq->cq;
	qp_init_attr.recv_cq=dcq->cq;

	qp_init_attr.cap.max_send_wr=15000;
	qp_init_attr.cap.max_send_sge=1;

	qp_init_attr.cap.max_recv_wr=15000;
	qp_init_attr.cap.max_recv_sge=1;

	retval=rdma_create_qp(rdma_trans->cm_id,
	                        rdma_trans->device->pd,
	                        &qp_init_attr);
	if(retval)
	{
		ERROR_LOG("rdma create qp error.");
		goto cleanhcq;
	}

	rdma_trans->qp=rdma_trans->cm_id->qp;
	rdma_trans->dcq=dcq;

	return retval;

cleanhcq:
	free(dcq);
	return retval;
}

static void dhmp_qp_release(struct dhmp_transport* rdma_trans)
{
	if(rdma_trans->qp)
	{
		ibv_destroy_qp(rdma_trans->qp);
		ibv_destroy_cq(rdma_trans->dcq->cq);
		dhmp_context_del_event_fd(rdma_trans->ctx,
								rdma_trans->dcq->comp_channel->fd);
		free(rdma_trans->dcq);
		rdma_trans->dcq=NULL;
	}
}


static int on_cm_addr_resolved(struct rdma_cm_event* event, struct dhmp_transport* rdma_trans)
{
	int retval=0;

	retval=rdma_resolve_route(rdma_trans->cm_id, ROUTE_RESOLVE_TIMEOUT);
	if(retval)
	{
		ERROR_LOG("RDMA resolve route error.");
		return retval;
	}

	return retval;
}

static int on_cm_route_resolved(struct rdma_cm_event* event, struct dhmp_transport* rdma_trans)
{
	struct rdma_conn_param conn_param;
	int i, retval=0;

	retval=dhmp_qp_create(rdma_trans);
	if(retval)
	{
		ERROR_LOG("hmr qp create error.");
		return retval;
	}

	memset(&conn_param, 0, sizeof(conn_param));
	conn_param.retry_count=100;
	conn_param.rnr_retry_count=200;
	conn_param.responder_resources = 1;
	conn_param.initiator_depth = 1;

	retval=rdma_connect(rdma_trans->cm_id, &conn_param);
	if(retval)
	{
		ERROR_LOG("rdma connect error.");
		goto cleanqp;
	}

	dhmp_post_all_recv(rdma_trans);
	return retval;

cleanqp:
	dhmp_qp_release(rdma_trans);
	rdma_trans->ctx->stop=1;
	rdma_trans->trans_state=DHMP_TRANSPORT_STATE_ERROR;
	return retval;
}

static struct dhmp_transport* dhmp_is_exist_connection(struct sockaddr_in *sock)
{
	char cur_ip[INET_ADDRSTRLEN], travers_ip[INET_ADDRSTRLEN];
	struct dhmp_transport *rdma_trans=NULL, *res_trans=NULL;
	struct in_addr in=sock->sin_addr;
	int cur_ip_len,travers_ip_len;
	
	inet_ntop(AF_INET, &(sock->sin_addr), cur_ip, sizeof(cur_ip));
	cur_ip_len=strlen(cur_ip);
	
	pthread_mutex_lock(&server->mutex_client_list);
	list_for_each_entry(rdma_trans, &server->client_list, client_entry)
	{
		inet_ntop(AF_INET, &(rdma_trans->peer_addr.sin_addr), travers_ip, sizeof(travers_ip));
		travers_ip_len=strlen(travers_ip);
		
		if(memcmp(cur_ip, travers_ip, max(cur_ip_len,travers_ip_len))==0)
		{
			INFO_LOG("find the same connection.");
			res_trans=rdma_trans;
			break;
		}
	}
	pthread_mutex_unlock(&server->mutex_client_list);

	return res_trans;
}

static int on_cm_connect_request(struct rdma_cm_event* event, 
										struct dhmp_transport* rdma_trans)
{
	struct dhmp_transport* new_trans,*normal_trans;
	struct rdma_conn_param conn_param;
	int i,retval=0;
	char* peer_addr;

	normal_trans=dhmp_is_exist_connection(&event->id->route.addr.dst_sin);
	if(normal_trans)
		new_trans=dhmp_transport_create(rdma_trans->ctx, rdma_trans->device,
									false, true);
	else
		new_trans=dhmp_transport_create(rdma_trans->ctx, rdma_trans->device,
									false, false);
	if(!new_trans)
	{
		ERROR_LOG("rdma trans process connect request error.");
		return -1;
	}
	
	new_trans->cm_id=event->id;
	event->id->context=new_trans;

	
	retval = dhmp_qp_create(new_trans);
	if(retval)
	{
		ERROR_LOG("dhmp qp create error.");
		goto out;
	}

	new_trans->node_id = server->cur_connections;
	++server->cur_connections;
	server->connect_trans[new_trans->node_id] = new_trans;

	pthread_mutex_lock(&server->mutex_client_list);
	list_add_tail(&new_trans->client_entry, &server->client_list);
	pthread_mutex_unlock(&server->mutex_client_list);
	
	memset(&conn_param, 0, sizeof(conn_param));
	conn_param.retry_count=100;
	conn_param.rnr_retry_count=200;
	conn_param.responder_resources = 1;
	conn_param.initiator_depth = 1;
	
	size_t data_size = 1048576; // 1MB req size
	//#2 for L5
	amper_allocspace_for_server(new_trans, 3, data_size); // L5
	
	//#2 for L5 
	retval=rdma_accept(new_trans->cm_id, &conn_param);
	if(retval)
	{
		ERROR_LOG("rdma accept error.");
		return -1;
	}
	
	new_trans->trans_state=DHMP_TRANSPORT_STATE_CONNECTING;
	dhmp_post_all_recv(new_trans);

	return retval;

out:
	free(new_trans);
	return retval;
}

static int on_cm_established(struct rdma_cm_event* event, struct dhmp_transport* rdma_trans)
{
	int retval=0;

	memcpy(&rdma_trans->local_addr,
			&rdma_trans->cm_id->route.addr.src_sin,
			sizeof(rdma_trans->local_addr));

	memcpy(&rdma_trans->peer_addr,
			&rdma_trans->cm_id->route.addr.dst_sin,
			sizeof(rdma_trans->peer_addr));
	
	rdma_trans->trans_state=DHMP_TRANSPORT_STATE_CONNECTED;
	return retval;
}

/**
 *	dhmp_destroy_source: destroy the used RDMA resouces
 */
static void dhmp_destroy_source(struct dhmp_transport* rdma_trans)
{
	if(rdma_trans->send_mr.addr)
	{
		ibv_dereg_mr(rdma_trans->send_mr.mr);
		free(rdma_trans->send_mr.addr);
	}

	if(rdma_trans->recv_mr.addr)
	{
		ibv_dereg_mr(rdma_trans->recv_mr.mr);
		free(rdma_trans->recv_mr.addr);
	}
	
	rdma_destroy_qp(rdma_trans->cm_id);
	dhmp_context_del_event_fd(rdma_trans->ctx, rdma_trans->dcq->comp_channel->fd);
	dhmp_context_del_event_fd(rdma_trans->ctx, rdma_trans->event_channel->fd);
}

static int on_cm_disconnected(struct rdma_cm_event* event, struct dhmp_transport* rdma_trans)
{
	dhmp_destroy_source(rdma_trans);
	rdma_trans->trans_state=DHMP_TRANSPORT_STATE_DISCONNECTED;
	if(server!=NULL)
	{
		--server->cur_connections;
		pthread_mutex_lock(&server->mutex_client_list);
		list_del(&rdma_trans->client_entry);
		pthread_mutex_unlock(&server->mutex_client_list);
	}
	
	return 0;
}

static int on_cm_error(struct rdma_cm_event* event, struct dhmp_transport* rdma_trans)
{
	dhmp_destroy_source(rdma_trans);
	rdma_trans->trans_state=DHMP_TRANSPORT_STATE_ERROR;
	if(server!=NULL)
	{
		--server->cur_connections;
		pthread_mutex_lock(&server->mutex_client_list);
		list_del(&rdma_trans->client_entry);
		pthread_mutex_unlock(&server->mutex_client_list);
	}
	return 0;
}

/*
 *	the function use for handling the event of event channel
 */
static int dhmp_handle_ec_event(struct rdma_cm_event* event)
{
	int retval=0;
	struct dhmp_transport* rdma_trans;
	
	rdma_trans=(struct dhmp_transport*) event->id->context;

	INFO_LOG("cm event [%s],status:%d",
	           rdma_event_str(event->event),event->status);

	switch(event->event)
	{
		case RDMA_CM_EVENT_ADDR_RESOLVED:
			retval=on_cm_addr_resolved(event, rdma_trans);
			break;
		case RDMA_CM_EVENT_ROUTE_RESOLVED:
			retval=on_cm_route_resolved(event, rdma_trans);
			break;
		case RDMA_CM_EVENT_CONNECT_REQUEST:
			retval=on_cm_connect_request(event,rdma_trans);
			break;
		case RDMA_CM_EVENT_ESTABLISHED:
			retval=on_cm_established(event,rdma_trans);
			break;
		case RDMA_CM_EVENT_DISCONNECTED:
			retval=on_cm_disconnected(event,rdma_trans);
			break;
		case RDMA_CM_EVENT_CONNECT_ERROR:
			retval=on_cm_error(event, rdma_trans);
			break;
		default:
			//ERROR_LOG("occur the other error.");
			retval=-1;
			break;
	};

	return retval;
}


static void dhmp_event_channel_handler(int fd, void* data)
{
	struct rdma_event_channel* ec=(struct rdma_event_channel*) data;
	struct rdma_cm_event* event,event_copy;
	int retval=0;

	event=NULL;
	while(( retval=rdma_get_cm_event(ec, &event) ==0))
	{
		memcpy(&event_copy, event, sizeof(*event));

		/*
		 * note: rdma_ack_cm_event function will clear event content
		 * so need to copy event content into event_copy.
		 */
		rdma_ack_cm_event(event);

		if(dhmp_handle_ec_event(&event_copy))
			break;
	}

	if(retval && errno!=EAGAIN)
	{
		ERROR_LOG("rdma get cm event error.");
	}
}

static int dhmp_event_channel_create(struct dhmp_transport* rdma_trans)
{
	int flags,retval=0;

	rdma_trans->event_channel=rdma_create_event_channel();
	if(!rdma_trans->event_channel)
	{
		ERROR_LOG("rdma create event channel error.");
		return -1;
	}

	flags=fcntl(rdma_trans->event_channel->fd, F_GETFL, 0);
	if(flags!=-1)
		flags=fcntl(rdma_trans->event_channel->fd,
		              F_SETFL, flags|O_NONBLOCK);

	if(flags==-1)
	{
		retval=-1;
		ERROR_LOG("set event channel nonblock error.");
		goto clean_ec;
	}

	dhmp_context_add_event_fd(rdma_trans->ctx,
								EPOLLIN,
	                            rdma_trans->event_channel->fd,
	                            rdma_trans->event_channel,
	                            dhmp_event_channel_handler);
	return retval;

clean_ec:
	rdma_destroy_event_channel(rdma_trans->event_channel);
	return retval;
}

static int dhmp_memory_register(struct ibv_pd *pd, 
									struct dhmp_mr *dmr, size_t length)
{
	dmr->addr=nvm_malloc(length);
	if(!dmr->addr)
	{
		ERROR_LOG("allocate mr memory error.");
		return -1;
	}

	dmr->mr=ibv_reg_mr(pd, dmr->addr, length, IBV_ACCESS_LOCAL_WRITE);
	if(!dmr->mr)
	{
		ERROR_LOG("rdma register memory error.");
		goto out;
	}

	dmr->cur_pos=0;
	return 0;

out:
	nvm_free(dmr->addr);
	return -1;
}

struct dhmp_transport* dhmp_transport_create(struct dhmp_context* ctx, 
													struct dhmp_device* dev,
													bool is_listen,
													bool is_poll_qp)
{
	struct dhmp_transport *rdma_trans;
	int err=0;
	
	rdma_trans=(struct dhmp_transport*)malloc(sizeof(struct dhmp_transport));
	if(!rdma_trans)
	{
		ERROR_LOG("allocate memory error");
		return NULL;
	}

	rdma_trans->trans_state=DHMP_TRANSPORT_STATE_INIT;
	rdma_trans->ctx=ctx;
	rdma_trans->device=dev;
	rdma_trans->nvm_used_size=0;
	
	err=dhmp_event_channel_create(rdma_trans);
	if(err)
	{
		ERROR_LOG("dhmp event channel create error");
		goto out;
	}

	if(!is_listen)
	{
		err=dhmp_memory_register(rdma_trans->device->pd,
								&rdma_trans->send_mr,
								SEND_REGION_SIZE);
		if(err)
			goto out_event_channel;

		err=dhmp_memory_register(rdma_trans->device->pd,
								&rdma_trans->recv_mr,
								RECV_REGION_SIZE);
		if(err)
			goto out_send_mr;
		
		rdma_trans->is_poll_qp=is_poll_qp;
	}
	
	return rdma_trans;
out_send_mr:
	ibv_dereg_mr(rdma_trans->send_mr.mr);
	free(rdma_trans->send_mr.addr);
	
out_event_channel:
	dhmp_context_del_event_fd(rdma_trans->ctx, rdma_trans->event_channel->fd);
	rdma_destroy_event_channel(rdma_trans->event_channel);
	
out:
	free(rdma_trans);
	return NULL;
}

int dhmp_transport_listen(struct dhmp_transport* rdma_trans, int listen_port)
{
	int retval=0, backlog;
	struct sockaddr_in addr;

	retval=rdma_create_id(rdma_trans->event_channel,
	                        &rdma_trans->cm_id,
	                        rdma_trans, RDMA_PS_TCP);
	if(retval)
	{
		ERROR_LOG("rdma create id error.");
		return retval;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family=AF_INET;
	addr.sin_port=htons(listen_port);

	retval=rdma_bind_addr(rdma_trans->cm_id,
	                       (struct sockaddr*) &addr);
	if(retval)
	{
		ERROR_LOG("rdma bind addr error.");
		goto cleanid;
	}

	backlog=10;
	retval=rdma_listen(rdma_trans->cm_id, backlog);
	if(retval)
	{
		ERROR_LOG("rdma listen error.");
		goto cleanid;
	}

	rdma_trans->trans_state=DHMP_TRANSPORT_STATE_LISTEN;
	INFO_LOG("rdma listening on port %d",
	           ntohs(rdma_get_src_port(rdma_trans->cm_id)));

	return retval;

cleanid:
	rdma_destroy_id(rdma_trans->cm_id);
	rdma_trans->cm_id=NULL;

	return retval;
}

static int dhmp_port_uri_transfer(struct dhmp_transport* rdma_trans,
										const char* url, int port)
{
	struct sockaddr_in peer_addr;
	int retval=0;

	memset(&peer_addr,0,sizeof(peer_addr));
	peer_addr.sin_family=AF_INET;
	peer_addr.sin_port=htons(port);

	retval=inet_pton(AF_INET, url, &peer_addr.sin_addr);
	if(retval<=0)
	{
		ERROR_LOG("IP Transfer Error.");
		goto out;
	}

	memcpy(&rdma_trans->peer_addr, &peer_addr, sizeof(struct sockaddr_in));

out:
	return retval;
}

int dhmp_transport_connect(struct dhmp_transport* rdma_trans,
                             const char* url, int port)
{
	int retval=0;
	if(!url||port<=0)
	{
		ERROR_LOG("url or port input error.");
		return -1;
	}

	retval=dhmp_port_uri_transfer(rdma_trans, url, port);
	if(retval<0)
	{
		ERROR_LOG("rdma init port uri error.");
		return retval;
	}

	/*rdma_cm_id dont init the rdma_cm_id's verbs*/
	retval=rdma_create_id(rdma_trans->event_channel,
						&rdma_trans->cm_id,
						rdma_trans, RDMA_PS_TCP);
	if(retval)
	{
		ERROR_LOG("rdma create id error.");
		goto clean_rdmatrans;
	}
	retval=rdma_resolve_addr(rdma_trans->cm_id, NULL,
	                          (struct sockaddr*) &rdma_trans->peer_addr,
	                           ADDR_RESOLVE_TIMEOUT);
	if(retval)
	{
		ERROR_LOG("RDMA Device resolve addr error.");
		goto clean_cmid;
	}
	
	rdma_trans->trans_state=DHMP_TRANSPORT_STATE_CONNECTING;
	return retval;

clean_cmid:
	rdma_destroy_id(rdma_trans->cm_id);

clean_rdmatrans:
	rdma_trans->cm_id=NULL;

	return retval;
}

/*
 *	two sided RDMA operations
 */
static void dhmp_post_recv(struct dhmp_transport* rdma_trans, void *addr)
{
	struct ibv_recv_wr recv_wr, *bad_wr_ptr=NULL;
	struct ibv_sge sge;
	struct dhmp_task *recv_task_ptr;
	int err=0;

	if(rdma_trans->trans_state>DHMP_TRANSPORT_STATE_CONNECTED)
		return ;
	
	recv_task_ptr=dhmp_recv_task_create(rdma_trans, addr);
	if(!recv_task_ptr)
	{
		ERROR_LOG("create recv task error.");
		return ;
	}
	
	recv_wr.wr_id=(uintptr_t)recv_task_ptr;
	recv_wr.next=NULL;
	recv_wr.sg_list=&sge;
	recv_wr.num_sge=1;

	sge.addr=(uintptr_t)recv_task_ptr->sge.addr;
	sge.length=recv_task_ptr->sge.length;
	sge.lkey=recv_task_ptr->sge.lkey;
	
	err=ibv_post_recv(rdma_trans->qp, &recv_wr, &bad_wr_ptr);
	if(err)
		ERROR_LOG("ibv post recv error.");
	
}

/**
 *	dhmp_post_all_recv:loop call the dhmp_post_recv function
 */
static void dhmp_post_all_recv(struct dhmp_transport *rdma_trans)
{
	int i, single_region_size=0;

	if(rdma_trans->is_poll_qp)
		single_region_size=SINGLE_POLL_RECV_REGION;
	else
		single_region_size=SINGLE_NORM_RECV_REGION;
	
	for(i=0; i<RECV_REGION_SIZE/single_region_size; i++)
	{
		dhmp_post_recv(rdma_trans, 
			rdma_trans->recv_mr.addr+i*single_region_size);
	}
}

void dhmp_post_send(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg_ptr)
{
	struct ibv_send_wr send_wr,*bad_wr=NULL;
	struct ibv_sge sge;
	struct dhmp_task *send_task_ptr;
	int err=0;
	
	if(rdma_trans->trans_state!=DHMP_TRANSPORT_STATE_CONNECTED)
		return ;
	send_task_ptr=dhmp_send_task_create(rdma_trans, msg_ptr);
	if(!send_task_ptr)
	{
		ERROR_LOG("create recv task error.");
		return ;
	}
	
	memset ( &send_wr, 0, sizeof ( send_wr ) );
	send_wr.wr_id= ( uintptr_t ) send_task_ptr;
	send_wr.sg_list=&sge;
	send_wr.num_sge=1;
	send_wr.opcode=IBV_WR_SEND;
	send_wr.send_flags=IBV_SEND_SIGNALED;

	sge.addr= ( uintptr_t ) send_task_ptr->sge.addr;
	sge.length=send_task_ptr->sge.length;
	sge.lkey=send_task_ptr->sge.lkey;

	err=ibv_post_send ( rdma_trans->qp, &send_wr, &bad_wr );
	if ( err )
		ERROR_LOG ( "ibv_post_send error." );

}

struct dhmp_send_mr* dhmp_create_smr_per_ops(struct dhmp_transport* rdma_trans, void* addr, int length )
{
	struct dhmp_send_mr *res,*tmp;
	void* new_addr=NULL;

	res=(struct dhmp_send_mr* )malloc(sizeof(struct dhmp_send_mr));
	if(!res)
	{
		ERROR_LOG("allocate memory error.");
		return NULL;
	}
	
	res->mr=ibv_reg_mr(rdma_trans->device->pd,
						addr, length, IBV_ACCESS_LOCAL_WRITE|IBV_ACCESS_REMOTE_READ);
	if(!res->mr)
	{
		ERROR_LOG("ibv register memory error.");
		goto error;
	}
	
	return res;

error:
	free ( res );
	return NULL;
}

int dhmp_rdma_read(struct dhmp_transport* rdma_trans, struct ibv_mr* mr, void* local_addr, int length)
{
	INFO_LOG("dhmp_rdma_read");
	struct dhmp_task* read_task;
	struct ibv_send_wr send_wr,*bad_wr=NULL;
	struct ibv_sge sge;
	struct dhmp_send_mr* temp_mr=NULL;
	int err=0;
	
	memcpy(client->per_ops_mr_addr,local_addr ,length);
	temp_mr=client->per_ops_mr;
	read_task=dhmp_read_task_create(rdma_trans, temp_mr, length);
	if ( !read_task )
	{
		ERROR_LOG ( "allocate memory error." );
		return -1;
	}

	memset(&send_wr, 0, sizeof(struct ibv_send_wr));

	send_wr.wr_id= ( uintptr_t ) read_task;
	send_wr.opcode=IBV_WR_RDMA_READ;
	send_wr.sg_list=&sge;
	send_wr.num_sge=1;
	send_wr.send_flags=IBV_SEND_SIGNALED;
	send_wr.wr.rdma.remote_addr=(uintptr_t)mr->addr;
	send_wr.wr.rdma.rkey=mr->rkey;

	sge.addr=(uintptr_t)read_task->sge.addr;
	sge.length=read_task->sge.length;
	sge.lkey=read_task->sge.lkey;
	
	err=ibv_post_send(rdma_trans->qp, &send_wr, &bad_wr);
	if(err)
	{
		ERROR_LOG("ibv_post_send error");
		goto error;
	}

	DEBUG_LOG("before local addr is %s", local_addr);
	
	while(!read_task->done_flag);
		
	DEBUG_LOG("local addr content is %s", local_addr);

	return 0;
error:
	return -1;
}

int dhmp_rdma_write ( struct dhmp_transport* rdma_trans, struct dhmp_addr_info *addr_info, struct ibv_mr* mr, void* local_addr, int length, int dram_flag )
{
	struct dhmp_task* write_task;
	struct dhmp_send_mr* temp_mr=NULL;
	int err=0;

	memcpy(client->per_ops_mr_addr,local_addr ,length);
	temp_mr=client->per_ops_mr;
	write_task=dhmp_write_task_create(rdma_trans, temp_mr, length);
	if(!write_task)
	{
		ERROR_LOG("allocate memory error.");
		return -1;
	}
	write_task->addr_info=addr_info;
	
	amper_post_write(write_task, mr, write_task->sge.addr, write_task->sge.length, write_task->sge.lkey, false);
#ifdef FLUSH
	struct dhmp_task* read_task;
	read_task=dhmp_read_task_create(rdma_trans, client->per_ops_mr2, length);
	if ( !read_task )
	{
		ERROR_LOG ( "allocate memory error." );
		return -1;
	}
	amper_post_read(read_task, mr, read_task->sge.addr, 0 read_task->sge.lkey, false);

	DEBUG_LOG("before read_test_mr addr is %s", client->per_ops_mr2->mr->addr);
	while(!write_task->done_flag);
	while(!read_task->done_flag);			
	DEBUG_LOG("read_test_mr addr content is %s", client->per_ops_mr2->mr->addr);
#else
	while(!write_task->done_flag);
#endif
	
	return 0;
	
error:
	return -1;
}

int getFreeList()
{
	return 0;
}

void dhmp_ReqAddr1_request_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg)
{
	struct dhmp_ReqAddr1_response response;
	struct dhmp_msg res_msg;

	memcpy ( &response.req_info, msg->data, sizeof(struct dhmp_ReqAddr1_request));
	INFO_LOG ( "client ReqAddr1 size %d",response.req_info.req_size);

	res_msg.msg_type=DHMP_MSG_REQADDR1_RESPONSE;
	res_msg.data_size=sizeof(struct dhmp_ReqAddr1_response);
	res_msg.data=&response;

	int index = getFreeList();
	server->tasklist[index].dhmp_addr = response.req_info.dhmp_addr;
	server->tasklist[index].rdma_trans = rdma_trans;
	server->tasklist[index].length = response.req_info.req_size;
	server->tasklist[index].cmpflag = response.req_info.cmpflag;

	response.task_offset = index;

	dhmp_post_send(rdma_trans, &res_msg);
	return ;
}

void dhmp_ReqAddr1_response_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg)
{
	struct dhmp_ReqAddr1_response * response_msg;
	/*memcpy(&response_msg, msg->data, sizeof(struct dhmp_ReqAddr1_response));*/
	response_msg = msg->data;
	struct reqAddr_work * work = response_msg->req_info.task;

	work->dhmp_addr = response_msg->req_info.dhmp_addr;  /*main des*/
	work->length = response_msg->task_offset;
	work->recv_flag = true;
	/*DEBUG_LOG("response ReqAddr1 addr %p ",response_msg.req_info.dhmp_addr); */
}


int dhmp_post_writeImm ( struct dhmp_transport* rdma_trans, struct dhmp_addr_info *addr_info, 
					struct ibv_mr* mr, void* local_addr, int length, uint32_t task_offset)
{
	struct dhmp_task* write_task;
	struct ibv_send_wr send_wr,*bad_wr=NULL;
	struct ibv_sge sge;
	struct dhmp_send_mr* temp_mr=NULL;
	int err=0;
	
	memcpy(client->per_ops_mr_addr,local_addr ,length);
	temp_mr=client->per_ops_mr;
	write_task=dhmp_write_task_create(rdma_trans, temp_mr, length);
	if(!write_task)
	{
		ERROR_LOG("allocate memory error.");
		return -1;
	}
	write_task->addr_info=addr_info;
	
	memset(&send_wr, 0, sizeof(struct ibv_send_wr));

	send_wr.wr_id= ( uintptr_t ) write_task;
	send_wr.opcode=IBV_WR_RDMA_WRITE_WITH_IMM;
	send_wr.sg_list=&sge;
	send_wr.num_sge=1;
	send_wr.send_flags=IBV_SEND_SIGNALED;
	send_wr.wr.rdma.remote_addr= ( uintptr_t ) mr->addr;
	send_wr.wr.rdma.rkey=mr->rkey;
	send_wr.imm_data = task_offset;/*(htonl)*/

	sge.addr= ( uintptr_t ) write_task->sge.addr;
	sge.length=write_task->sge.length;
	sge.lkey=write_task->sge.lkey;

	err=ibv_post_send ( rdma_trans->qp, &send_wr, &bad_wr );
	if ( err )
	{
		ERROR_LOG("ibv_post_send error");
		exit(-1);
		return -1;
	}
	
	while(!write_task->done_flag);
	
	return 0;
}

void dhmp_WriteImm_request_handler(uint32_t task_offset)
{
	int index = (int) task_offset;
	struct dhmp_WriteImm_response response;
	struct dhmp_msg res_msg;

	response.cmpflag = server->tasklist[index].cmpflag;

	size_t length = server->tasklist[index].length;
	INFO_LOG ( "client writeImm size %d",length);

	/* get server addr from dhmp_addr & copy & flush*/
	void * server_addr =server->tasklist[index].dhmp_addr;
	_mm_clflush(server_addr);

	res_msg.msg_type=DHMP_MSG_WriteImm_RESPONSE;
	res_msg.data_size=sizeof(struct dhmp_WriteImm_response);
	res_msg.data=&response;
	
	dhmp_post_send(server->tasklist[index].rdma_trans, &res_msg); /*next use writeImm to responde*/
	
	return ;

}

void dhmp_WriteImm_response_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg)
{
	struct dhmp_WriteImm_response * response_msg = msg->data;
	/*memcpy(&response_msg, msg->data, sizeof(struct dhmp_WriteImm_response)); */
	*(response_msg->cmpflag) = true;
}


int dhmp_post_writeImm2( struct dhmp_transport* rdma_trans, struct dhmp_WriteImm2_request *msg, 
							struct ibv_mr* mr, void* local_addr)
{
	struct dhmp_task* write_task;
	struct ibv_send_wr send_wr,*bad_wr=NULL;
	struct ibv_sge sge;
	struct dhmp_send_mr* temp_mr=NULL;
	int err=0;
	int length ;

	length =  sizeof(struct dhmp_WriteImm2_request);
	memcpy(client->per_ops_mr_addr,msg ,length);
	temp_mr=client->per_ops_mr;
	write_task=dhmp_write_task_create(rdma_trans, temp_mr, length);
	if(!write_task)
	{
		ERROR_LOG("allocate memory error.");
		return -1;
	}
	
	memset(&send_wr, 0, sizeof(struct ibv_send_wr));

	send_wr.wr_id= ( uintptr_t ) write_task;
	send_wr.opcode=IBV_WR_RDMA_WRITE_WITH_IMM;
	send_wr.sg_list=&sge;
	send_wr.num_sge=1;
	send_wr.send_flags=IBV_SEND_SIGNALED;
	send_wr.wr.rdma.remote_addr= ( uintptr_t ) mr->addr;
	send_wr.wr.rdma.rkey=mr->rkey;
	send_wr.imm_data = NUM_writeImm2;/*(htonl)*/

	sge.addr= ( uintptr_t ) write_task->sge.addr;
	sge.length=write_task->sge.length;
	sge.lkey=write_task->sge.lkey;

	err=ibv_post_send ( rdma_trans->qp, &send_wr, &bad_wr );
	if ( err )
	{
		ERROR_LOG("ibv_post_send error");
		exit(-1);
		return -1;
	}
	
	while(!write_task->done_flag);
	return 0;

}

void dhmp_WriteImm2_request_handler(struct dhmp_transport* rdma_trans)
{
	struct dhmp_WriteImm2_response response;
	struct dhmp_msg res_msg;

	memcpy ( &response.req_info, (server->ringbufferAddr + server->cur_addr), sizeof(struct dhmp_WriteImm2_request));
	INFO_LOG ( "client writeImm2 size %d",response.req_info.req_size);

	size_t length = response.req_info.req_size;

	void * server_addr = response.req_info.server_addr;
	_mm_clflush(server_addr);

	res_msg.msg_type=DHMP_MSG_WriteImm2_RESPONSE;
	res_msg.data_size=sizeof(struct dhmp_WriteImm2_response);
	res_msg.data=&response;
	
	dhmp_post_send(rdma_trans, &res_msg); /*next use writeImm to responde*/
	
	return ;

}

void dhmp_WriteImm2_response_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg)
{
	struct dhmp_WriteImm2_response * response_msg = msg->data;
	/*memcpy(&response_msg, msg->data, sizeof(struct dhmp_WriteImm2_response)); */
	struct dhmp_writeImm2_work * work = response_msg->req_info.task;

	work->recv_flag = true;
}


int dhmp_post_writeImm3( struct dhmp_transport* rdma_trans, struct dhmp_WriteImm3_request *msg, 
							struct ibv_mr* mr, void* local_addr)
{
	struct dhmp_task* write_task;
	struct ibv_send_wr send_wr,*bad_wr=NULL;
	struct ibv_sge sge;
	struct dhmp_send_mr* temp_mr=NULL;
	int err=0;
	
	int length = msg->req_size + sizeof(struct dhmp_WriteImm3_request);
	void * temp = client->per_ops_mr_addr;

	memcpy(temp, msg, sizeof(struct dhmp_WriteImm3_request));
	memcpy(temp + sizeof(struct dhmp_WriteImm3_request), local_addr, msg->req_size);
	temp_mr=client->per_ops_mr;
	write_task=dhmp_write_task_create(rdma_trans, temp_mr, length);
	if(!write_task)
	{
		ERROR_LOG("allocate memory error.");
		return -1;
	}
	
	memset(&send_wr, 0, sizeof(struct ibv_send_wr));

	send_wr.wr_id= ( uintptr_t ) write_task;
	send_wr.opcode=IBV_WR_RDMA_WRITE_WITH_IMM;
	send_wr.sg_list=&sge;
	send_wr.num_sge=1;
	send_wr.send_flags=IBV_SEND_SIGNALED;
	send_wr.wr.rdma.remote_addr= ( uintptr_t ) mr->addr;
	send_wr.wr.rdma.rkey=mr->rkey;
	send_wr.imm_data = NUM_writeImm3;/*(htonl)*/

	sge.addr= ( uintptr_t ) write_task->sge.addr;
	sge.length=write_task->sge.length;
	sge.lkey=write_task->sge.lkey;

	err=ibv_post_send ( rdma_trans->qp, &send_wr, &bad_wr );
	if ( err )
	{
		ERROR_LOG("ibv_post_send error");
		exit(-1);
		return -1;
	}
	
	while(!write_task->done_flag);
	
	return 0;

}


void dhmp_WriteImm3_request_handler(struct dhmp_transport* rdma_trans)
{
	INFO_LOG("writeimm3 request handler start");
	struct dhmp_WriteImm3_response response;
	struct dhmp_msg res_msg;

	memcpy ( &response.req_info, (server->ringbufferAddr + server->cur_addr), sizeof(struct dhmp_WriteImm3_request));
	INFO_LOG ( "client writeImm size %d",response.req_info.req_size);


	void * context_ptr = (void *)(server->ringbufferAddr + server->cur_addr + sizeof(struct dhmp_WriteImm3_request));

	size_t length = response.req_info.req_size;
	void * dhmp_addr = response.req_info.dhmp_addr;
	/* get server addr from dhmp_addr & copy & flush*/
	void * server_addr = dhmp_addr;
	memcpy(server_addr , context_ptr ,length);
	_mm_clflush(server_addr);

	res_msg.msg_type=DHMP_MSG_WriteImm3_RESPONSE;
	res_msg.data_size=sizeof(struct dhmp_WriteImm3_response);
	res_msg.data=&response;
	
	dhmp_post_send(rdma_trans, &res_msg); /*next use writeImm to responde*/
	INFO_LOG("writeimm3 request handler over");
	return ;

}

void dhmp_WriteImm3_response_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg)
{
	struct dhmp_WriteImm3_response * response_msg = msg->data;
	/*memcpy(&response_msg, msg->data, sizeof(struct dhmp_WriteImm3_response)); */
	struct dhmp_writeImm3_work * work = response_msg->req_info.task;

	work->recv_flag = true;
}

int dhmp_rdma_write2 ( struct dhmp_transport* rdma_trans, struct dhmp_addr_info *addr_info, struct ibv_mr* mr, void* local_addr, int length, int dram_flag )
{
	struct dhmp_task* write_task;
	struct dhmp_send_mr* temp_mr=NULL;
	int err=0;
	
	memcpy(client->per_ops_mr_addr,local_addr ,length);
	temp_mr=client->per_ops_mr;
	write_task=dhmp_write_task_create(rdma_trans, temp_mr, length);
	if(!write_task)
	{
		ERROR_LOG("allocate memory error.");
		return -1;
	}
	write_task->addr_info=addr_info;
	
	amper_post_write(write_task, mr, write_task->sge.addr, write_task->sge.length, write_task->sge.lkey, false);

	while(!write_task->done_flag);
	
	return 0;
}

void dhmp_send2_request_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg)
{
	struct dhmp_Send2_response response;
	struct dhmp_msg res_msg;

	memcpy ( &response.req_info, msg->data, sizeof(struct dhmp_Send2_request));
	size_t length = response.req_info.req_size;
	INFO_LOG ( "client operate size %d",length);

	void * server_addr = response.req_info.server_addr;
	_mm_clflush(server_addr);

	res_msg.msg_type=DHMP_MSG_SEND2_RESPONSE;
	res_msg.data_size=sizeof(struct dhmp_Send2_response);
	res_msg.data=&response;

	dhmp_post_send(rdma_trans, &res_msg);
	return ;
}

void dhmp_send2_response_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg)
{
	struct dhmp_Send2_response* response_msg = msg->data;
	struct dhmp_Send2_work * task = response_msg->req_info.task;
	task->recv_flag = true;
	DEBUG_LOG("response flush %p",response_msg->req_info.server_addr);
}

int dhmp_post_write2( struct dhmp_transport* rdma_trans, struct dhmp_Write2_request *msg, 
							struct ibv_mr* mr, void* local_addr)
{
	struct dhmp_task* write_task;
	struct ibv_send_wr send_wr,*bad_wr=NULL;
	struct ibv_sge sge;
	struct dhmp_send_mr* temp_mr=NULL;
	int err=0;
	
	int length = msg->req_size + sizeof(struct dhmp_Write2_request);
	void * temp = client->per_ops_mr_addr;
	memcpy(temp, msg, sizeof(struct dhmp_Write2_request));
	memcpy(temp + sizeof(struct dhmp_Write2_request), local_addr, msg->req_size);
	temp_mr=client->per_ops_mr;
	write_task=dhmp_write_task_create(rdma_trans, temp_mr, length);
	if(!write_task)
	{
		ERROR_LOG("allocate memory error.");
		return -1;
	}
	
	memset(&send_wr, 0, sizeof(struct ibv_send_wr));

	send_wr.wr_id= ( uintptr_t ) write_task;
	send_wr.opcode=IBV_WR_RDMA_WRITE;
	send_wr.sg_list=&sge;
	send_wr.num_sge=1;
	send_wr.send_flags=IBV_SEND_SIGNALED;
	send_wr.wr.rdma.remote_addr= ( uintptr_t ) mr->addr;
	send_wr.wr.rdma.rkey=mr->rkey;

	sge.addr= ( uintptr_t ) write_task->sge.addr;
	sge.length=write_task->sge.length;
	sge.lkey=write_task->sge.lkey;

	err=ibv_post_send ( rdma_trans->qp, &send_wr, &bad_wr );
	if ( err )
	{
		ERROR_LOG("ibv_post_send error");
		exit(-1);
		return -1;
	}
	
	while(!write_task->done_flag);
	return 0;

}


void dhmp_Write2_request_handler()
{

	struct dhmp_Write2_response response;
	struct dhmp_msg res_msg;

	memcpy ( &response.req_info, (server->ringbufferAddr + server->cur_addr), sizeof(struct dhmp_Write2_request));
	INFO_LOG ( "client write2 size %d",response.req_info.req_size);

	void * context_ptr = (void *)(server->ringbufferAddr + server->cur_addr +sizeof(struct dhmp_Write2_request));
	size_t length = response.req_info.req_size;
	void * server_addr = response.req_info.dhmp_addr;
	memcpy(server_addr, context_ptr, length);
	_mm_clflush(server_addr);

	res_msg.msg_type=DHMP_MSG_Write2_RESPONSE;
	res_msg.data_size=sizeof(struct dhmp_Write2_response);
	res_msg.data=&response;
	dhmp_post_send(server->rdma_trans, &res_msg); /*next use writeImm to responde*/
	return ;

}

void dhmp_Write2_response_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg)
{
	struct dhmp_Write2_response * response_msg = msg->data;
	/*memcpy(&response_msg, msg->data, sizeof(struct dhmp_Write2_response)); */
	struct dhmp_rw2_work * work = response_msg->req_info.task;

	work->recv_flag = true;
}

void dhmp_Sread_read(struct dhmp_transport* rdma_trans, struct ibv_mr* rmr, struct ibv_mr* lmr, int length)
{
	struct dhmp_task* read_task = malloc(sizeof(struct dhmp_task));
	struct ibv_send_wr send_wr2,*bad_wr2=NULL;
	struct ibv_sge sge2;

	memset(&send_wr2, 0, sizeof(struct ibv_send_wr));

	read_task->rdma_trans = rdma_trans;
	read_task->done_flag = false;

	send_wr2.wr_id= ( uintptr_t ) read_task;
	send_wr2.opcode=IBV_WR_RDMA_READ;
	send_wr2.sg_list=&sge2;
	send_wr2.num_sge=1; // or 0
	send_wr2.send_flags=IBV_SEND_SIGNALED;
	send_wr2.wr.rdma.remote_addr=(uintptr_t)rmr->addr;
	send_wr2.wr.rdma.rkey= rmr->rkey;

	sge2.addr=(uintptr_t)lmr->addr;
	sge2.length= length; 
	sge2.lkey= lmr->lkey;
	
	int err=ibv_post_send(rdma_trans->qp, &send_wr2, &bad_wr2);
	if(err)
	{
		ERROR_LOG("ibv_post_send error");
		return ;
	}
	while(!read_task->done_flag);
	free(read_task);			
}

void dhmp_Sread_server()
{
	struct dhmp_Sread_response response;
	struct dhmp_msg res_msg;

	memcpy ( &response.req_info, (server->ringbufferAddr + server->cur_addr), sizeof(struct dhmp_Sread_request));
	INFO_LOG ( "client sread size %d",response.req_info.req_size);

	/*read*/
	dhmp_Sread_read(server->rdma_trans, &(response.req_info.client_mr), 
				&(response.req_info.server_mr), response.req_info.req_size);

	void * server_addr = response.req_info.server_mr.addr;
	_mm_clflush(server_addr);

	res_msg.msg_type=DHMP_MSG_Sread_RESPONSE;
	res_msg.data_size=sizeof(struct dhmp_Sread_response);
	res_msg.data=&response;

	dhmp_post_send(server->rdma_trans, &res_msg);
	return ;
}

void dhmp_Sread_request_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg)
{
	struct dhmp_Sread_response response;
	struct dhmp_msg res_msg;

	memcpy ( server->ringbufferAddr, msg->data, sizeof(struct dhmp_Sread_request));
	server->rdma_trans = rdma_trans;
	server->model_C_new_msg = true;
	return ;
}

void dhmp_Sread_response_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg)
{
	struct dhmp_Sread_response* response_msg = msg->data;
	struct dhmp_Sread_work * task = response_msg->req_info.task;
	task->recv_flag = true;
	DEBUG_LOG("response flush %p",response_msg->req_info.server_mr.addr);
}

void amper_tailwindRPC_request_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg)
{
	struct dhmp_TailwindRPC_response response;
	struct dhmp_msg res_msg;

	memcpy ( &response.req_info, msg->data, sizeof(struct dhmp_TailwindRPC_request));

	void * context_ptr = msg->data + sizeof(struct dhmp_TailwindRPC_request);
	size_t length = response.req_info.req_size;
	void * server_addr = response.req_info.dhmp_addr;
	memcpy(server_addr, context_ptr, length);
	_mm_clflush(server_addr);

	res_msg.msg_type=DHMP_MSG_Tailwind_RPC_response;
	res_msg.data_size=sizeof(struct dhmp_TailwindRPC_response);
	res_msg.data=&response;
	dhmp_post_send(rdma_trans, &res_msg); 
	return ;
}

void amper_tailwindRPC_response_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg)
{
	struct dhmp_TailwindRPC_response* response_msg = msg->data;
	struct amper_Tailwind_work * task = response_msg->req_info.task;
	task->recv_flag = true;
	DEBUG_LOG("tailwindRPC flush ");
}

void amper_DaRPC_request_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg)
{
	struct dhmp_DaRPC_response response;
	struct dhmp_msg res_msg;

	memcpy ( &response.req_info, msg->data, sizeof(struct dhmp_DaRPC_request));

	void * context_ptr = msg->data + sizeof(struct dhmp_DaRPC_request);
	size_t single_length = *(size_t*)context_ptr;
	void * server_addr = nvm_malloc(single_length);
	int i;
	for(;i < BATCH;i++)
	{
		memcpy(server_addr, context_ptr, single_length);
		_mm_clflush(server_addr);
	}
	res_msg.msg_type=DHMP_MSG_DaRPC_response;
	res_msg.data_size=sizeof(struct dhmp_DaRPC_response);
	res_msg.data=&response;
	dhmp_post_send(rdma_trans, &res_msg); /
	return ;
}

void amper_DaRPC_response_handler(struct dhmp_transport* rdma_trans, struct dhmp_msg* msg)
{
	struct dhmp_DaRPC_response* response_msg = msg->data;
	struct amper_DaRPC_work * task = response_msg->req_info.task;
	task->recv_flag = true;
	DEBUG_LOG("DaRPC flush ");

	pthread_mutex_lock(&client->mutex_request_num);
	mutex_request_num ++;
	pthread_mutex_unlock(&client->mutex_request_num);
}
