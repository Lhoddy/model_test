#ifndef DHMP_WORK_H
#define DHMP_WORK_H

struct dhmp_rw_work{
	struct dhmp_transport *rdma_trans;
	void *dhmp_addr;
	void *local_addr;
	struct dhmp_send_mr *smr;
	size_t length;
	bool done_flag;
};

struct amper_clover_work{
	struct dhmp_transport *rdma_trans;
	void *dhmp_addr;
	bool done_flag;
};

struct amper_L5_work{
	struct dhmp_transport *rdma_trans;
	void *local_addr;
	size_t length;
	bool done_flag;
};

struct amper_Tailwind_work{
	struct dhmp_transport *rdma_trans;
	void *local_addr;
	size_t length;
	size_t offset;
	bool done_flag;
	bool recv_flag;
};

struct amper_DaRPC_work{
	struct dhmp_transport *rdma_trans;
	void *local_addr;
	size_t length;
	bool done_flag;
	bool recv_flag;
};


struct amper_scalable_work{
	struct dhmp_transport *rdma_trans;
	size_t length;
	bool done_flag;
};


struct dhmp_rw2_work{
	struct dhmp_transport *rdma_trans;
	size_t length;
	void * dhmp_addr;
	void * local_addr;
	bool done_flag;
	bool recv_flag;
	struct ibv_mr * S_ringMR;
};

struct dhmp_malloc_work{
	struct dhmp_transport *rdma_trans;
	struct dhmp_addr_info *addr_info;
	void *res_addr;
	size_t length;
	bool done_flag;
	int is_special; // 1 = ringbuffer ;2 = clover
};

struct dhmp_free_work{
	struct dhmp_transport *rdma_trans;
	void *dhmp_addr;
	bool done_flag;
};

struct dhmp_close_work{
	struct dhmp_transport *rdma_trans;
	bool done_flag;
};

struct dhmp_Send_work{
	struct dhmp_transport *rdma_trans;
	size_t length;
	bool done_flag;
	void * dhmp_addr;
	void * local_addr;
	bool recv_flag;
	bool is_write; //or read
};

struct dhmp_Send2_work{
	struct dhmp_transport *rdma_trans;
	size_t length;
	void * dhmp_addr;
	void * server_addr;
	void * local_addr;
	bool recv_flag;
	bool done_flag;
};


struct reqAddr_work{
	struct dhmp_transport *rdma_trans;
	size_t length;
	bool done_flag;
	bool recv_flag;
	void * dhmp_addr;
	bool * cmpflag;
};

struct dhmp_writeImm_work{
	struct dhmp_transport *rdma_trans;
	size_t length;
	void * dhmp_addr;
	void * local_addr;
	bool done_flag;
	bool recv_flag;
	uint32_t task_offset;
};

struct dhmp_writeImm2_work{
	struct dhmp_transport *rdma_trans;
	size_t length;
	void * dhmp_addr;
	void * server_addr;
	void * local_addr;
	bool done_flag;
	bool recv_flag;
	struct ibv_mr * S_ringMR;
};

struct dhmp_writeImm3_work{
	struct dhmp_transport *rdma_trans;
	size_t length;
	void * dhmp_addr;
	void * local_addr;
	bool done_flag;
	bool recv_flag;
	struct ibv_mr * S_ringMR;
};

struct dhmp_Sread_work{
	struct dhmp_transport *rdma_trans;
	size_t length;
	void * dhmp_addr;
	void * local_addr;
	bool recv_flag;
	bool done_flag;
};

enum dhmp_work_type{
	DHMP_WORK_MALLOC,
	DHMP_WORK_FREE,
	DHMP_WORK_READ,
	DHMP_WORK_WRITE,
	DHMP_WORK_CLOSE,
	DHMP_WORK_DONE,
	DHMP_WORK_SEND,
	DHMP_WORK_SEND2,
	DHMP_WORK_REQADDR1,
	DHMP_WORK_WRITEIMM,
	DHMP_WORK_WRITEIMM2,
	DHMP_WORK_WRITEIMM3,
	DHMP_WORK_SREAD,
	DHMP_WORK_WRITE2,
	AMPER_WORK_L5
};


struct dhmp_work{
	enum dhmp_work_type work_type;
	void *work_data;
	struct list_head work_entry;
};

void *dhmp_work_handle_thread(void *data);
int dhmp_hash_in_client(void *addr);
void *dhmp_transfer_dhmp_addr(struct dhmp_transport *rdma_trans,
									void *normal_addr);
struct dhmp_addr_info *dhmp_get_addr_info_from_ht(int index, void *dhmp_addr);


int amper_post_write(struct dhmp_task* task, struct ibv_mr* rmr, uint64_t* sge_addr, uint32_t sge_length,uint32_t sge_lkey ,bool is_inline);
int amper_post_read(struct dhmp_task* task, struct ibv_mr* rmr, uint64_t* sge_addr, uint32_t sge_length,uint32_t sge_lkey ,bool is_inline);
#endif



