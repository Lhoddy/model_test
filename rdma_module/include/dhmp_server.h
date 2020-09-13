#ifndef DHMP_SERVER_H
#define DHMP_SERVER_H
/*decide the buddy system's order*/
#define buddy_MAX_ORDER 5
#define SINGLE_AREA_SIZE 4194304  


extern const size_t buddy_size[buddy_MAX_ORDER];

struct dhmp_free_block{
	void *addr;
	size_t size;
	struct ib_mr *mr;
	struct list_head free_block_entry;
};

struct dhmp_free_block_head{
	struct list_head free_block_list;
	int nr_free;
};

struct dhmp_area{
	/*be able to alloc max size=buddy_size[max_index]*/
	int max_index;
	//pthread_spinlock_t mutex_area;
	struct ib_mr *mr;
	struct list_head area_entry;
	struct dhmp_free_block_head block_head[buddy_MAX_ORDER];
	void *server_addr;
};
struct dhmp_tasklist{
	struct dhmp_transport *rdma_trans;
	size_t length;
	void * dhmp_addr;
	bool * cmpflag;
};

struct dhmp_server{

	struct dhmp_transport *listen_trans;
	struct dhmp_transport *connect_trans[DHMP_CLIENT_NODE_NUM];

	struct list_head dev_list;
	// pthread_mutex_t mutex_client_list;
	struct list_head client_list;

	/*below structure about area*/
	struct dhmp_area *cur_area;
	struct list_head area_list;
	struct list_head more_area_list;

	int cur_connections;
	long nvm_used_size;
	long nvm_total_size;
	
	void * ringbufferAddr;
	uint64_t cur_addr;
	struct dhmp_transport* rdma_trans;
	struct dhmp_tasklist tasklist[30];

	int client_num;

	struct ib_mr* mr;
	void * addr;

};

extern struct dhmp_server *server;

struct dhmp_area *dhmp_area_create(bool is_init_buddy, size_t length);
struct ib_mr * dhmp_malloc_poll_area(size_t length);



void amper_allocspace_for_server(struct dhmp_transport* rdma_trans, int is_special, size_t size);
//#1 for RFP
#endif


