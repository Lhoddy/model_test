#include <stdio.h>

#include "dhmp.h"
#define  obj_num_max  50005

// #define octopus 
// #define clover  
// #define L5  
// #define Tailwind  
// #define DaRPC   
// #define scaleRPC
// #define RFP   

const double A = 1.3;  
const double C = 1.0;  

double pf[obj_num_max]; 
int rand_num[obj_num_max]={0};
struct timespec task_time_start, task_time_end;
	unsigned long task_time_diff_ns;
void show(struct timespec* time, int output)
{
	return ;

	
}

void generate()
{
    int i;
    double sum = 0.0;
 
    for (i = 0; i < obj_num_max; i++)
        sum += C/pow((double)(i+2), A);

    for (i = 0; i < obj_num_max; i++)
    {
        if (i == 0)
            pf[i] = C/pow((double)(i+2), A)/sum;
        else
            pf[i] = pf[i-1] + C/pow((double)(i+2), A)/sum;
    }
}
void pick()
{
	int i, index;

    generate();

    srand(time(0));
    for ( i= 0; i < obj_num_max; i++)
    {
        index = 0;
        double data = (double)rand()/RAND_MAX; 
        while (index<(obj_num_max-6)&&data > pf[index])   
            index++;
		rand_num[i]=index;
    }
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
		objnum=50000;//atoi(argv[2]);
		accessnum = 300000;//atoi(argv[3]);
		write_part = atoi(argv[2]);
	}
	pick();
	str=malloc(size+8);
	if(!str){
		printf("alloc mem error");
		return -1;
	}
	memset(str, 0 , size+8);
	snprintf(str, size, "hello world hello");
	
	dhmp_client_init(size,objnum);

#ifdef octopus
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0); // 8 = point space for c&s
	dhmp_malloc(size,10); //for octo
	show(&task_time_start,0);
	for(j=0;j<(accessnum /100 )* write_part ;j++)
	{ 
		i = j%objnum;
		model_1_octopus(addr[rand_num[i]], size, str,1);
	}
	for(;j<accessnum;j++)
	{ 
		i = j%objnum;
		model_1_octopus(addr[rand_num[i]], size, str,0);
	}

	show(&task_time_end,1);

	for(i=0;i<objnum;i++)	
		dhmp_free(addr[i]);
#endif
#ifdef L5
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	dhmp_malloc(size,3); //for L5

	

	for(j=0;j<(accessnum /100 )* write_part;j++)
	{ 
		i = j%objnum;
		model_5_L5(size, str, addr[rand_num[i]], 1); //1 for write
	}
	for(;j<accessnum;j++)
	{ 
		i = j%objnum;
		model_5_L5(size, str, addr[rand_num[i]], 0); //1 for write
	}
	// model_5_L5(size, str, addr[rand_num[i]], 0); //0 for read

	

	for(i=0;i<objnum;i++)	
		dhmp_free(addr[i]);
#endif
#ifdef FaRM
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	dhmp_malloc(size,8); //for FaRM


	for(j=0;j<(accessnum /100 )* write_part;j++)
	{ 
		i = j%objnum;
		model_FaRM(str, size, addr[rand_num[i]], 1); //1 for write
	}
	for(;j<accessnum;j++)
	{ 
		i = j%objnum;
		model_FaRM(str, size, addr[rand_num[i]], 0); //1 for write
	}
	show(&task_time_end,1);

	check_request(1);
	for(i=0;i<objnum;i++)	
		dhmp_free(addr[i]);
#endif
#ifdef RFP
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	dhmp_malloc(size,6); //RFP

	show(&task_time_start,0);

	for(j=0;j<(accessnum /100 )* write_part;j++)
	{ 
		i = j%objnum;
		model_4_RFP(size, str, addr[rand_num[i]], 1); //1 for write
	}
	for(;j<accessnum;j++)
	{ 
		i = j%objnum;
		model_4_RFP(size, str, addr[rand_num[i]], 0); //1 for write
	}

	show(&task_time_end,1);

	for(i=0;i<objnum;i++)	
		dhmp_free(addr[i]);
#endif
#ifdef scaleRPC
	batch = BATCH;
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	dhmp_malloc(size,7); //scaleRPC
	uintptr_t remote_addr;
	local_addr = (uintptr_t)str;
	show(&task_time_start,0);
	for(j=0;j<(accessnum /100 )* write_part;j=j+batch)
	{ 
		i = j%objnum;
		remote_addr = (uintptr_t)addr[rand_num[i]];
		model_7_scalable(&remote_addr, size, &local_addr, 1 , batch);
	}
	for(;j<accessnum;j=j+batch)
	{ 
		i = j%objnum;
		remote_addr = (uintptr_t)addr[rand_num[i]];
		model_7_scalable(&remote_addr, size, &local_addr, 0 , batch);
	}
	show(&task_time_end,1);
	for(i=0;i<objnum;i++)	
		dhmp_free(addr[i]);
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
	show(&task_time_start,0);
	for(j=0;j<(accessnum /100 )* write_part;j=j+batch)
	{ 
		i = j%objnum;
		for(k = 0;k<batch;k++)
			temp[k] = (uintptr_t)(addr[rand_num[(i+k)%objnum]]);
		model_3_DaRPC(temp, size, &local_addr, 1 , batch);//用的默认的send recv queue
	}
	for(;j<accessnum;j=j+batch)
	{ 
		i = j%objnum;
		for(k = 0;k<batch;k++)
			temp[k] = (uintptr_t)(addr[rand_num[(i+k)%objnum]]);
		model_3_DaRPC(temp, size, &local_addr, 0 , batch); //用的默认的send recv queue
	}

	show(&task_time_end,1);
	for(i=0;i<objnum;i++)	
		dhmp_free(addr[i]);
#endif
#ifdef FaSST
//need #define UD
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	batch = 1;
	int k;
	uintptr_t* temp = malloc(batch*sizeof(uintptr_t));
	local_addr = (uintptr_t)str;
	show(&task_time_start,0);
	for(j=0;j<(accessnum /100 )* write_part;j=j+batch)
	{ 
		i = j%objnum;
		for(k = 0;k<batch;k++)
			temp[k] = (uintptr_t)addr[rand_num[(i+k)%objnum]];
		send_UD(temp, size, &local_addr, 1 , batch);//用的默认的send recv queue
	}
	for(;j<accessnum;j=j+batch)
	{ 
		i = j%objnum;
		for(k = 0;k<batch;k++)
			temp[k] = (uintptr_t)addr[rand_num[(i+k)%objnum]];
		send_UD(temp, size, &local_addr , 0 , batch); //用的默认的send recv queue
	}
check_request(3);
	show(&task_time_end,1);
	for(i=0;i<objnum;i++)	
		dhmp_free(addr[i]);
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
		model_herd(str, size, addr[rand_num[i]], 1); //1 for write
	}
	for(;j<accessnum;j++)
	{ 
		i = j%objnum;
		model_herd(str, size, addr[rand_num[i]], 0); //1 for write
	}
	show(&task_time_end,1);
	for(i=0;i<objnum;i++)	
		dhmp_free(addr[i]);
#endif
	
	
	dhmp_client_destroy();

	
	return 0;
}



