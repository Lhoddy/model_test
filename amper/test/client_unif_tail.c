#include <stdio.h>

#include "dhmp.h"
#define  obj_num_max  505

struct timespec task_time_start, task_time_end;
	unsigned long task_time_diff_ns;
void show(struct timespec* time, int output)
{
	return ;

	
}

int main(int argc,char *argv[])
{
#ifndef clover
	void* addr[obj_num_max];
#endif
	int i,j,size,rnum;
	char *str;
	int objnum,accessnum, write_part;
	char batch;
	uintptr_t local_addr;
	
	if(argc<3)
	{
		printf("input param error. input:<filname> <size> <objnum> <accessnum>\n");
		printf("note:max of <objnum> = max of <accessnum> == 10000\n");
		return -1;
	}
	else
	{
		size=atoi(argv[1]);
		objnum=500;//atoi(argv[2]);
		accessnum = 2000;//atoi(argv[3]);
		write_part = atoi(argv[2]);
	}
	str=malloc(size+8);
	if(!str){
		printf("alloc mem error");
		return -1;
	}
	memset(str, 0 , size+8);
	snprintf(str, size, "hello world hello");
	
	dhmp_client_init(size,objnum,write_part);

#ifdef octopus
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0); // 8 = point space for c&s
	dhmp_malloc(size,10); //for octo
	for(j=0;j<(accessnum /100 )* write_part ;j++)
	{ 
		i = j%objnum;
		model_1_octopus(addr[i], size, str,1);
	}
	for(;j<accessnum;j++)
	{ 
		i = j%objnum;
		model_1_octopus(addr[i], size, str,0);
	}


#endif
#ifdef L5
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	dhmp_malloc(size,3); //for L5

	

	for(j=0;j<(accessnum /100 )* write_part;j++)
	{ 
		i = j%objnum;
		model_5_L5(size, str, addr[i], 1); //1 for write
	}
	for(;j<accessnum;j++)
	{ 
		i = j%objnum;
		model_5_L5(size, str, addr[i], 0); //1 for write
	}

#endif
#ifdef FaRM
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	dhmp_malloc(size,8); //for FaRM


	for(j=0;j<(accessnum /100 )* write_part;j++)
	{ 
		i = j%objnum;
		model_FaRM(str, size, addr[i], 1); //1 for write
	}
	for(;j<accessnum;j++)
	{ 
		i = j%objnum;
		model_FaRM(str, size, addr[i], 0); //1 for write
	}
	show(&task_time_end,1);

	check_request(1);
#endif
#ifdef RFP
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	dhmp_malloc(size,6); //RFP

	for(j=0;j<(accessnum /100 )* write_part;j++)
	{ 
		i = j%objnum;
		model_4_RFP(size, str,(uintptr_t) addr[i], 1); //1 for write
	}
	for(;j<accessnum;j++)
	{ 
		i = j%objnum;
		model_4_RFP(size, str,(uintptr_t) addr[i], 0); //1 for write
	}


#endif
#ifdef scaleRPC
	batch = BATCH;
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	dhmp_malloc(size,7); //scaleRPC
	uintptr_t remote_addr;
	local_addr = (uintptr_t)str;
	for(j=0;j<(accessnum /100 )* write_part;j=j+batch)
	{ 
		i = j%objnum;
		remote_addr = (uintptr_t)addr[i];
		model_7_scalable(&remote_addr, size, &local_addr, 1 , batch);
	}
	for(;j<accessnum;j=j+batch)
	{ 
		i = j%objnum;
		remote_addr = (uintptr_t)addr[i];
		model_7_scalable(&remote_addr, size, &local_addr, 0 , batch);
	}
#endif
#ifdef DaRPC
	//need DaRPC_SERVER at dhmp.h only for server if CQ cluster
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	dhmp_malloc(0,5); //DaRPC
	batch = BATCH;
	int k;
	uintptr_t* temp = malloc(batch*sizeof(uintptr_t));
	local_addr = (uintptr_t)str;
	for(j=0;j<(accessnum /100 )* write_part;j=j+batch)
	{ 
		i = j%objnum;
		for(k = 0;k<batch;k++)
			temp[k] = (uintptr_t)(addr[(i+k)%objnum]);
		model_3_DaRPC(temp, size, &local_addr, 1 , batch);//用的默认的send recv queue
	}
	for(;j<accessnum;j=j+batch)
	{ 
		i = j%objnum;
		for(k = 0;k<batch;k++)
			temp[k] = (uintptr_t)(addr[(i+k)%objnum]);
		model_3_DaRPC(temp, size, &local_addr, 0 , batch); //用的默认的send recv queue
	}


#endif
#ifdef FaSST
//need #define UD
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	batch = BATCH;
	int k;
	uintptr_t* temp = malloc(batch*sizeof(uintptr_t));
	local_addr = (uintptr_t)str;
	show(&task_time_start,0);
	for(j=0;j<(accessnum /100 )* write_part;j=j+batch)
	{ 
		i = j%objnum;
		for(k = 0;k<batch;k++)
			temp[k] = (uintptr_t)addr[(i+k)%objnum];
		send_UD(temp, size, &local_addr, 1 , batch);//用的默认的send recv queue
	}
	for(;j<accessnum;j=j+batch)
	{ 
		i = j%objnum;
		for(k = 0;k<batch;k++)
			temp[k] = (uintptr_t)addr[(i+k)%objnum];
		send_UD(temp, size, &local_addr , 0 , batch); //用的默认的send recv queue
	}
check_request(3);
	show(&task_time_end,1);
#endif
#ifdef herd
//need #define UD
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	dhmp_malloc(size,9); //for Herd

	show(&task_time_start,0);

	for(j=0;j<(accessnum /100 )* write_part;j++)
	{ 
		i = j%objnum;
		model_herd(str, size, addr[i], 1); //1 for write
	}
	for(;j<accessnum;j++)
	{ 
		i = j%objnum;
		model_herd(str, size, addr[i], 0); //1 for write
	}
	show(&task_time_end,1);
	
#endif
	
	for(i=0;i<objnum;i++)	
		dhmp_free(addr[i]);
	dhmp_client_destroy();

	
	return 0;
}



