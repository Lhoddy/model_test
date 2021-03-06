#include <stdio.h>

#include "dhmp.h"
#define  obj_num_max  50005


void show(const char * str, struct timespec* time)
{
	fprintf(stderr,str);	
	fflush(stderr);
	clock_gettime(CLOCK_MONOTONIC, time);	
}


int main(int argc,char *argv[])
{
#ifndef clover
	void* addr[obj_num_max];
#endif
	int i,j,size,rnum;
	char *str;
	int objnum,accessnum, write_part;
	struct timespec task_time_start, task_time_end;
	unsigned long task_time_diff_ns;
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
		objnum=50000;//atoi(argv[2]);
		accessnum = 300000;//atoi(argv[3]);
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
		addr[i]=dhmp_malloc(size+8,0); // 8 = point space for c&s
	dhmp_malloc(size,10); //for octo
	show("start count",&task_time_start);
	// dhmp_malloc(size,8); //似乎做不了
	for(j=0;j<(accessnum /100 )* write_part ;j++)
	{ 
		i = j%objnum;
		model_1_octopus(addr[i], size, str,1);
	}
	for(;j<accessnum;j++)
	{ 
		i = j%objnum;
		model_1_octopus_R(addr[i], size, str,0);
	}

	show("over count",&task_time_end);

#endif
#ifdef L5
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	dhmp_malloc(size,3); //for L5

	show("start count",&task_time_start);

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
	// model_5_L5(size, str, addr[i], 0); //0 for read

	show("over count",&task_time_end);
#endif
#ifdef FaRM
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	dhmp_malloc(size,8); //for FaRM

	show("start count",&task_time_start);

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
	show("over count",&task_time_end);
	check_request(1);
	
#endif
#ifdef RFP
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	dhmp_malloc(size,6); //RFP

	show("start count",&task_time_start);

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

	show("over count",&task_time_end);

#endif
#ifdef scaleRPC
	batch = BATCH;
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	dhmp_malloc(size,7); //scaleRPC
	uintptr_t remote_addr;
	local_addr = (uintptr_t)str;
	show("start count",&task_time_start);
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
	show("over count",&task_time_end);
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
	show("start count",&task_time_start);
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

	show("over count",&task_time_end);
#endif
#ifdef FaSST
//need #define UD
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	batch = BATCH;
	int k;
	uintptr_t* temp = malloc(batch*sizeof(uintptr_t));
	local_addr = (uintptr_t)str;
	show("start count",&task_time_start);
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
	show("over count",&task_time_end);
#endif
#ifdef herd
//need #define UD
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	dhmp_malloc(size,9); //for Herd

	show("start count",&task_time_start);

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
	show("over count",&task_time_end);
	
#endif
	
	task_time_diff_ns = ((task_time_end.tv_sec * 1000000000) + task_time_end.tv_nsec) -
                        ((task_time_start.tv_sec * 1000000000) + task_time_start.tv_nsec);
  	fprintf(stderr,"%lf\n",size, write_part, (double)task_time_diff_ns/1000000);
	fflush(stderr);
	for(i=0;i<objnum;i++)	
		dhmp_free(addr[i]);
	dhmp_client_destroy();

	
	return 0;
}



