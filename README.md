# model_test
RC/UC/UD in dhmp_qp_create()


static int join_handler(struct cmatest_node *node, struct rdma_ud_param *param)
{
	char buf[40];
	inet_ntop(AF_INET6, param->ah_attr.grh.dgid.raw, buf, 40);
	printf("mckey: joined dgid: %s\n", buf);
	node->remote_qpn = param->qp_num;
	node->remote_qkey = param->qkey;
	node->ah = ibv_create_ah(node->pd, &param->ah_attr);
	if (!node->ah)
	{
	printf("mckey: failure creating address handle\n");
	goto err;
	}
	node->connected = 1;
	test.connects_left--;
	return 0;
	err:
	connect_error();
	return -1;
}
switch (event->event)
	case RDMA_CM_EVENT_MULTICAST_JOIN:
	ret = join_handler(cma_id->context, &event->param.ud);
/
struct rdma_cm_id *cma_id, struct rdma_cm_event *event