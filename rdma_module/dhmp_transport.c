#include "dhmp.h"
#include "dhmp_log.h"
#include "dhmp_hash.h"
// #include "dhmp_context.h"
#include "dhmp_dev.h"
#include "dhmp_transport.h"
#include "dhmp_task.h"
#include "dhmp_work.h"

void *kernel_malloc(size_t size)
{
	return kmalloc(size , GFP_KERNEL);
}

void kernel_free(size_t size)
{
	kfree(size);
}

struct dhmp_transport* dhmp_transport_create(struct dhmp_context* ctx, 
													struct dhmp_device* dev,
													bool is_listen,
													bool is_poll_qp)
{
	struct dhmp_transport *rdma_trans;
	int err=0;
	
	rdma_trans=(struct dhmp_transport*)kernel_malloc(sizeof(struct dhmp_transport));
	if(!rdma_trans)
	{
		ERROR_LOG("allocate memory error");
		return NULL;
	}

	rdma_trans->trans_state=DHMP_TRANSPORT_STATE_INIT;
	rdma_trans->ctx=ctx;
	rdma_trans->device=dev;
	
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
	// ib_dereg_mr(rdma_trans->send_mr.mr);
	kernel_free(rdma_trans->send_mr.addr);
	
out_event_channel:
	// rdma_destroy_event_channel(rdma_trans->event_channel);
	
out:
	kernel_free(rdma_trans);
	return NULL;
}

static int dhmp_memory_register(struct ib_pd *pd, 
									struct dhmp_mr *dmr, size_t length)
{
	dmr->addr=kernel_malloc(length);
	if(!dmr->addr)
	{
		ERROR_LOG("allocate mr memory error.");
		return -1;
	}

	dmr->mr=ib_get_dma_mr(pd, IB_ACCESS_LOCAL_WRITE);
	if(IS_ERR(dmr->mr))
	{
		ERROR_LOG("rdma register memory error.");
		goto out;
	}

	dmr->cur_pos=0;
	return 0;

out:
	kernel_free(dmr->addr);
	return -1;
}

static void dhmp_comp_channel_handler(int fd, void* data)
{
	struct dhmp_cq* dcq =(struct dhmp_cq*) data;
	struct ib_cq* cq;
	void* cq_ctx;
	struct ib_wc wc;
	int err=0;

	err=ib_get_cq_event(dcq->comp_channel, &cq, &cq_ctx);
	if(err)
	{
		ERROR_LOG("ib get cq event error.");
		return ;
	}

	ib_ack_cq_events(dcq->cq, 1);
	err=ib_req_notify_cq(dcq->cq, 0);
	if(err)
	{
		ERROR_LOG("ib req notify cq error.");
		return ;
	}

	while(ib_poll_cq(dcq->cq, 1, &wc))
	{
		if(wc.status==IB_WC_SUCCESS)
			dhmp_wc_success_handler(&wc);
		else
			dhmp_wc_error_handler(&wc);
	}
}


static struct dhmp_cq* amper_cq_get(struct dhmp_device* device)
{
	struct dhmp_cq* dcq;
	int retval,flags=0;

	dcq=(struct dhmp_cq*) kmalloc(1,sizeof(struct dhmp_cq));
	if(!dcq)
	{
		ERROR_LOG("allocate the memory of struct dhmp_cq error.");
		return NULL;
	}

	dcq->cq=ib_create_cq(device->ib_device, dhmp_comp_channel_handler, NULL,NULL , 100, 0);
	if(IS_ERR(dcq->cq))
	{
		ERROR_LOG("ib create cq error.");
		goto cleanhcq;
	}

	retval=ib_req_notify_cq(dcq->cq, 0);
	if(retval)
	{
		ERROR_LOG("ib req notify cq error.");
		goto cleanhcq;
	}

	dcq->device=device;
	return dcq;

cleanhcq:
	free(dcq);

	return NULL;
}

static int amper_qp_create(struct dhmp_transport* rdma_trans)
{
	int retval=0;
	struct ib_qp_init_attr qp_init_attr;
	struct dhmp_cq* dcq;
	dcq=amper_cq_get(rdma_trans->device);
	if(!dcq)
	{
		ERROR_LOG("dhmp cq get error.");
		return -1;
	}
	memset(&qp_init_attr,0,sizeof(qp_init_attr));
	qp_init_attr.qp_context=rdma_trans;
	qp_init_attr.qp_type=IB_QPT_RC;
	qp_init_attr.send_cq=dcq->cq;
	qp_init_attr.recv_cq=dcq->cq;

	qp_init_attr.cap.max_send_wr=15000;
	qp_init_attr.cap.max_send_sge=1;

	qp_init_attr.cap.max_recv_wr=15000;
	qp_init_attr.cap.max_recv_sge=1;

	qp_init_attr.sq_sig_type = IB_SIGNAL_REQ_WR;
	qp_init_attr.event_handler = NULL;
	
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

static int on_cm_connect_request(struct rdma_cm_event* event, 
										struct dhmp_transport* rdma_trans)   //only for ud client  not ud server
{
	struct rdma_conn_param conn_param;
	int i,retval=0;

	retval = amper_qp_create(rdma_trans);
	if(retval)
	{
		ERROR_LOG("dhmp qp create error.");
		goto out;
	}

	rdma_trans->node_id = server->cur_connections;
	++server->cur_connections;
	server->connect_trans[rdma_trans->node_id] = rdma_trans;

	pthread_mutex_lock(&server->mutex_client_list);
	list_add_tail(&rdma_trans->client_entry, &server->client_list);
	pthread_mutex_unlock(&server->mutex_client_list);
	
	memset(&conn_param, 0, sizeof(conn_param));
	conn_param.retry_count=100;
	conn_param.rnr_retry_count=200;
	conn_param.responder_resources = 1;
	conn_param.initiator_depth = 1;
	retval=rdma_accept(new_trans->cm_id, &conn_param);
	if(retval)
	{
		ERROR_LOG("rdma accept error.");
		return -1;
	}
	
	server->client_num ++;
	new_trans->trans_state=DHMP_TRANSPORT_STATE_CONNECTING;
	// dhmp_post_all_recv(new_trans);

	return retval;

out:
	free(new_trans);
	return retval;
}

static int
amper_cm_handler(struct rdma_cm_id *cm_id, struct rdma_cm_event *event)
{
	int ret = 0;

	INFO_LOG("event %d status %d id %p rdma_trans %p\n", event->event,
		   event->status, cm_id, cm_id->context);

	switch (event->event) {
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		ret = on_cm_connect_request(event, cm_id->context);
		if (ret)
			ERROR_LOG("failed handle connect request %d\n", ret);
		break;
	case RDMA_CM_EVENT_ESTABLISHED:
		INFO_LOG("RDMA_CM_EVENT_ESTABLISHEE");
		break;
	case RDMA_CM_EVENT_ADDR_CHANGE:    /* FALLTHRU */
	case RDMA_CM_EVENT_DISCONNECTED:   /* FALLTHRU */
	case RDMA_CM_EVENT_DEVICE_REMOVAL: /* FALLTHRU */
	case RDMA_CM_EVENT_TIMEWAIT_EXIT:  /* FALLTHRU */
		// ret = isert_disconnected_handler(cm_id, event->event);
		break;
	case RDMA_CM_EVENT_REJECTED:       /* FALLTHRU */
	case RDMA_CM_EVENT_UNREACHABLE:    /* FALLTHRU */
	case RDMA_CM_EVENT_CONNECT_ERROR:
		// isert_connect_error(cm_id);
		break;
	default:
		INFO_LOG("Unhandled RDMA CMA event: %d\n", event->event);
		break;
	}
	return ret;
}

int dhmp_transport_listen(struct dhmp_transport* rdma_trans, int listen_port)
{
	int retval=0, backlog;
	struct sockaddr_in addr;
	rdma_trans->cm_id = rdma_create_id(amper_cm_handler,
	                        &rdma_trans->cm_id,
	                        RDMA_PS_TCP, IB_QPT_RC);
	if (!rdma_trans->cm_id)
	{
		ERROR_LOG("rdma_create_id error.");
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

	backlog=10; /* backlog=10 is arbitrary */
	INFO_LOG("rdma listen start.");
	retval=rdma_listen(rdma_trans->cm_id, backlog);
	if(retval)
	{
		ERROR_LOG("rdma listen error.");
		goto cleanid;
	}
	INFO_LOG("rdma listen success.");
	rdma_trans->trans_state=DHMP_TRANSPORT_STATE_LISTEN;

	return retval;

cleanid:
	rdma_destroy_id(rdma_trans->cm_id);
	rdma_trans->cm_id=NULL;

	return retval;
}


