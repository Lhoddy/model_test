#ifndef DHMP_CLIENT_H
#define DHMP_CLIENT_H

#define DHMP_CLIENT_HT_SIZE 251

//struct timespec 
//{
//	time_t tv_sec;/* Seconds */
//	long	 tv_nsec;/* Nanoseconds */
//};

struct dhmp_client{
	struct dhmp_context ctx;
	struct dhmp_config config;

	struct list_head dev_list;

	struct dhmp_transport *connect_trans;

	/*store the dhmp_addr_entry hashtable*/
	//pthread_mutex_t mutex_ht;
	struct hlist_head addr_info_ht[DHMP_CLIENT_HT_SIZE];
	
	pthread_mutex_t mutex_send_mr_list;
	struct list_head send_mr_list;

	/*use for node select*/
	int fifo_node_index;

	pthread_t work_thread;
	pthread_mutex_t mutex_work_list;
	struct list_head work_list;

	/*per cycle*/
	int access_total_num;
	size_t access_region_size;
	size_t pre_average_size;

	/*use for countint the num of sending server poll's packets*/
	int poll_num;
	
	struct dhmp_send_mr* cas_mr;

	struct dhmp_send_mr* per_ops_mr;
	void * per_ops_mr_addr;
	struct dhmp_send_mr* per_ops_mr2;
	void * per_ops_mr_addr2;

	struct ibv_mr* local_mr;
	struct ibv_mr read_mr_for_all;
	pthread_mutex_t mutex_request_num;
	int para_request_num;

	struct  {
		struct ibv_mr mr;
		size_t length;
		size_t cur;
	} ringbuffer;

	struct{
		struct ibv_mr mailbox_mr;
		uint64_t num_1;
		struct ibv_mr message_mr;
		struct ibv_mr read_mr;
		struct dhmp_send_mr* local_smr1;
	}L5;

	struct{
		struct ibv_mr mr;
	}Tailwind_buffer;

	

	struct{
		struct ibv_mr write_mr;
		struct ibv_mr read_mr;
		char time;
	}RFP;

	struct{
		struct ibv_mr Sreq_mr;
		struct ibv_mr Sdata_mr;
		struct ibv_mr* Creq_mr;
		struct ibv_mr* Cdata_mr;
	}Salable;
};

extern struct dhmp_client *client;
#endif


