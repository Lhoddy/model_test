#ifndef DHMP_H
#define DHMP_H

#include <linux/sched.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kallsyms.h>
#include <linux/mm.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <linux/socket.h>
#include <linux/sort.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/memory.h>
#include <linux/pagemap.h>

#include <linux/buffer_head.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/parser.h>
#include <linux/random.h>
#include <linux/jiffies.h>
#include <linux/device.h>
#include <linux/atomic.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/sock.h>
#include <linux/syscalls.h>

#include <linux/semaphore.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/spinlock_types.h>
#include <linux/hashtable.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/jiffies.h>

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/mman.h>
#include <linux/cdev.h>
#include <rdma/rdma_cm_ib.h>
#include <rdma/rdma_cm.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_user_verbs.h>

#define FLUSH

#define DHMP_ADDR_LEN 18
#define DHMA_NIC_NAME_LEN 10

#define DHMP_SERVER_DRAM_TH ((uint64_t)1024*1024*1024*1)

#define DHMP_SERVER_NODE_NUM 3
#define DHMP_CLIENT_NODE_NUM 20


//#define DaRPC_SERVER
#define DaRPC_clust_NUM  5
#define BATCH 10

#define DHMP_DEFAULT_SIZE 256
#define DHMP_DEFAULT_POLL_TIME 800000000

#define DHMP_MAX_OBJ_NUM 40000
#define DHMP_MAX_CLIENT_NUM 100


#define DHMP_RTT_TIME (6000)


#ifndef bool
#define bool char
#define true 1
#define false 0
#endif

enum dhmp_msg_type{
	DHMP_MSG_MALLOC_REQUEST,
	DHMP_MSG_MALLOC_RESPONSE,
	DHMP_MSG_MALLOC_ERROR,
	DHMP_MSG_FREE_REQUEST,
	DHMP_MSG_FREE_RESPONSE,
	DHMP_MSG_SERVER_INFO_REQUEST,
	DHMP_MSG_SERVER_INFO_RESPONSE,
	DHMP_MSG_CLOSE_CONNECTION,
	DHMP_MSG_SEND_REQUEST,
	DHMP_MSG_SEND_RESPONSE,
	DHMP_MSG_SEND2_REQUEST,
    DHMP_MSG_SEND2_RESPONSE,
	DHMP_MSG_REQADDR1_REQUEST,
	DHMP_MSG_REQADDR1_RESPONSE,
	DHMP_MSG_WriteImm_REQUEST,
	DHMP_MSG_WriteImm_RESPONSE,
	DHMP_MSG_WriteImm2_REQUEST,
	DHMP_MSG_WriteImm2_RESPONSE,
	DHMP_MSG_WriteImm3_REQUEST,
	DHMP_MSG_WriteImm3_RESPONSE,
	DHMP_MSG_Sread_REQUEST,
	DHMP_MSG_Sread_RESPONSE,
	DHMP_MSG_Write2_REQUEST,
	DHMP_MSG_Write2_RESPONSE,
	DHMP_MSG_Tailwind_RPC_requeset,
	DHMP_MSG_Tailwind_RPC_response,
	DHMP_MSG_DaRPC_requeset,
	DHMP_MSG_DaRPC_response,
	DHMP_MSG_UD_REQUEST,
	DHMP_MSG_UD_RESPONSE
};

/*struct dhmp_msg:use for passing control message*/
struct dhmp_msg{
	enum dhmp_msg_type msg_type;
	size_t data_size;
	void *data;
	bool is_next;
};

/*struct dhmp_addr_info is the addr struct in cluster*/
struct dhmp_addr_info{
	int read_cnt;
	int write_cnt;
	int node_index;
	bool write_flag;
	struct ib_mr nvm_mr;
	struct hlist_node addr_entry;
};

struct dhmp_server_addr_info{
	void* server_addr;
	struct hlist_node addr_entry;
};
/*dhmp malloc request msg*/
struct dhmp_mc_request{
	size_t req_size;
	struct dhmp_addr_info *addr_info;
	bool is_special;
	struct ib_mr mr;
	struct ib_mr mr2;
	void * task;
};

/*dhmp malloc response msg*/
struct dhmp_mc_response{
	struct dhmp_mc_request req_info;
	struct ib_mr mr;
	struct ib_mr mr2;
	struct ib_mr read_mr;
	void * server_addr;
	void * server_addr2;
};

/*dhmp free memory request msg*/
struct dhmp_free_request{
	struct dhmp_addr_info *addr_info;
	struct ib_mr mr;
void* dhmp_addr;
};

/*dhmp free memory response msg*/
struct dhmp_free_response{
	struct dhmp_addr_info *addr_info;
};


struct dhmp_Send_request{
	size_t req_size;
	void * dhmp_addr;
	void * task;
	void * local_addr;
	bool is_write;
};

struct dhmp_Send_response{
	struct dhmp_Send_request req_info;
};

struct dhmp_Send2_request{
	size_t req_size;
	void * server_addr;
	void * task;
};

struct dhmp_Send2_response{
	struct dhmp_Send2_request req_info;
};

struct dhmp_ReqAddr1_request{
	size_t req_size;
	void * dhmp_addr;
	void * task;
	bool * cmpflag;
};

struct dhmp_ReqAddr1_response{
	struct dhmp_ReqAddr1_request req_info;
	uint32_t task_offset;
};

struct dhmp_WriteImm_response
{
	bool * cmpflag;
};

struct dhmp_WriteImm2_request{
	enum dhmp_msg_type msg_type;
	size_t req_size;
	void * server_addr;
	void * task;
};

struct dhmp_WriteImm2_response{
	struct dhmp_WriteImm2_request req_info;
};

struct dhmp_WriteImm3_request{
	enum dhmp_msg_type msg_type;
	size_t req_size;
	void * dhmp_addr;
	void * task;
};

struct dhmp_WriteImm3_response{
	struct dhmp_WriteImm3_request req_info;
};

struct dhmp_Write2_request{
	bool is_new;
	size_t req_size;
	void * dhmp_addr;
	void * task;
};

struct dhmp_Write2_response{
	struct dhmp_Write2_request req_info;
};

struct dhmp_Sread_request{
	size_t req_size;
	void * task;
	struct ib_mr client_mr;
	struct ib_mr server_mr;
};

struct dhmp_Sread_response{
	struct dhmp_Sread_request req_info;
};


struct dhmp_TailwindRPC_request{
	size_t req_size;
	void * dhmp_addr;
	void * task;
};

struct dhmp_TailwindRPC_response{
	struct dhmp_TailwindRPC_request req_info;
};

struct dhmp_DaRPC_request{
	size_t req_size;
	void * task;
};

struct dhmp_DaRPC_response{
	struct dhmp_TailwindRPC_request req_info;
};


struct dhmp_UD_request{
	size_t req_size;
	void * task;
};

struct dhmp_UD_response{
	struct dhmp_UD_request req_info;
};



/**
 *	dhmp_malloc: remote alloc the size of length region
 */
struct dhmp_device *dhmp_get_dev_from_server(void);

void *dhmp_malloc(size_t length, int is_special);

/**
 *	dhmp_read:read the data from dhmp_addr, write into the local_buf
 */
int dhmp_read(void *dhmp_addr, void * local_buf, size_t count);

/**
 *	dhmp_write:write the data in local buf into the dhmp_addr position
 */
int dhmp_write(void *dhmp_addr, void * local_buf, size_t count);


int amper_clover_compare_and_set(void *dhmp_addr);

/**
 *	dhmp_free:release remote memory
 */
void dhmp_free(void *dhmp_addr);

/**
 *	dhmp_client_init:init the dhmp client
 */
void dhmp_client_init(size_t size, int obj_num);

/**
 *	dhmp_client_destroy:clean RDMA resources
 */
void dhmp_client_destroy(void);

/**
 *	dhmp_server_init:init server 
 *	include config.xml read, rdma listen,
 *	register memory, context run. 
 */
void dhmp_server_init(void);

/**
 *	dhmp_server_destroy: close the context,
 *	clean the rdma resource
 */
void dhmp_server_destroy(void);
int dhmp_handle_ec_event(struct rdma_cm_event* event);

int dhmp_send(void *dhmp_addr, void * local_buf, size_t count, bool is_write);

void *kernel_malloc(size_t size);
void kernel_free(void * addr);

void model_A_write(void * globle_addr, size_t length, void * local_addr);
void model_A_writeImm(void * globle_addr, size_t length, void * local_addr);
void model_B_write(void * globle_addr, size_t length, void * local_addr);
void model_B_writeImm(void * globle_addr, size_t length, void * local_addr);
void model_B_send(void * globle_addr, size_t length, void * local_addr);
void model_C_sread(void * globle_addr, size_t length, void * local_addr);
void model_D_write(void * server_addr, size_t length, void * local_addr);
void model_D_writeImm(void * server_addr, size_t length, void * local_addr);
void model_D_send(void * server_addr, size_t length, void * local_addr);

void model_1_octopus(void * globle_addr, size_t length, void * local_addr);
void model_1_clover(void * write_addr, size_t length, void * local_addr, void * cas_addr);
void model_4_RFP( size_t length, void * local_addr);
void model_5_L5( size_t length, void * local_addr);
void model_6_Tailwind(int accessnum, int obj_num,int *rand_num , size_t length, void * local_addr);
void model_3_DaRPC(int accessnum, int *rand_num ,size_t length, void * local_addr);
void model_7_scalable(int accessnum, int *rand_num , size_t length, void * local_addr);
// void model_1_clover(void * globle_addr, size_t length, void * local_addr);

void send_UD(void* local_buf,size_t length );
#endif
