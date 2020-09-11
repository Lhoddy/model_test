#ifndef DHMP_CLIENT_H
#define DHMP_CLIENT_H

#define DHMP_CLIENT_HT_SIZE 251

struct dhmp_client{
	struct dhmp_context ctx;

	struct list_head dev_list;

	struct dhmp_transport *connect_trans[DHMP_SERVER_NODE_NUM];

	/*store the dhmp_addr_entry hashtable*/
	//pthread_mutex_t mutex_ht;
	struct hlist_head addr_info_ht[DHMP_CLIENT_HT_SIZE];
	
	// pthread_mutex_t mutex_send_mr_list;
	struct list_head send_mr_list;

	/*use for node select*/
	int fifo_node_index;

	// pthread_t work_thread;
	// pthread_mutex_t mutex_work_list;
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
		struct ib_mr mr;
		size_t length;
		size_t cur;
	} ringbuffer;

	struct{
		struct ib_mr mailbox_mr;
		uint64_t num_1;
		struct ib_mr message_mr;
		struct ib_mr read_mr;
	}L5;

	struct ib_mr read_mr;
	// pthread_mutex_t mutex_request_num;
	int para_request_num;

};

extern struct dhmp_client *client;
#endif


