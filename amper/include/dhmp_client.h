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

	struct dhmp_transport *connect_trans[DHMP_SERVER_NODE_NUM];
	struct dhmp_transport *poll_trans[DHMP_SERVER_NODE_NUM];

	/*store the dhmp_addr_entry hashtable*/
	//pthread_mutex_t mutex_ht;
	struct hlist_head addr_info_ht[DHMP_CLIENT_HT_SIZE];

	int poll_ht_fd;
	struct timespec poll_interval;
	pthread_t poll_ht_thread;
	
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
	
	struct dhmp_send_mr* per_ops_mr;
	void * per_ops_mr_addr;
	struct dhmp_send_mr* per_ops_mr2;
	void * per_ops_mr_addr2;

	struct  {
		struct ibv_mr mr;
		size_t length;
		size_t cur;
	} ringbuffer;

	struct{
		struct ibv_mr mailbox_mr;
		size_t mailbox_offset;
		struct ibv_mr message_mr;
	}L5;
};

extern struct dhmp_client *client;
#endif

