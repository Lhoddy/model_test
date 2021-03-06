#include <stdio.h>

#include "dhmp.h"
#define  obj_num_max  10005

// #define octopus 
// #define clover  
// #define L5  
// #define Tailwind  
// #define DaRPC   
// #define scalable
// #define RFP   

const double A = 1.3;  
const double C = 1.0;  

double pf[obj_num_max]; 
int rand_num[obj_num_max]={0};

void show(const char * str, struct timespec* time)
{
	fprintf(stderr,str);	
	fflush(stderr);
	clock_gettime(CLOCK_MONOTONIC, time);	
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
	struct timespec task_time_start, task_time_end;
	unsigned long task_time_diff_ns;
	
	if(argc<3)
	{
		printf("input param error. input:<filname> <size> <objnum> <accessnum>\n");
		printf("note:max of <objnum> = max of <accessnum> == 10000\n");
		return -1;
	}
	else
	{
		size=atoi(argv[1]);
		objnum=10000;//atoi(argv[2]);
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
		addr[i]=dhmp_malloc(size+8,0); // 8 = point space for c&s
	show("start count",&task_time_start);
	
	for(j=0;j<(accessnum /100 )* write_part ;j++)
	{ 
		i = j%objnum;
		model_1_octopus(addr[rand_num[i]], size, str);
	}
	for(;j<accessnum;j++)
	{ 
		i = j%objnum;
		model_1_octopus_R(addr[rand_num[i]], size, str);
	}

	show("over count",&task_time_end);

	for(i=0;i<objnum;i++)	
		dhmp_free(addr[i]);
#endif
#ifdef clover
	void * point;
	void * addr[accessnum];
	point=dhmp_malloc(8 * obj_num_max,0); // 8 = point space for c&s
	for(i=0;i<accessnum;i++)
		addr[i]=dhmp_malloc(size+8,0); // 8 = point space for c&s
	//dhmp_malloc(objnum,2); // clover c&s point

	show("start count",&task_time_start);
	for(j=0;j<(accessnum /100 )* write_part;j++)
	{ 
		i = j%objnum;
		model_1_clover(addr[j], size, str, point,rand_num[i]);
	}
	for(;j<accessnum;j++)
	{ 
		i = j%objnum;
		model_1_clover_R(size, str, addr[i]);
	}

	show("over count",&task_time_end);

	dhmp_free(point);
	for(i=0;i<accessnum;i++)
		dhmp_free(addr[i]);
#endif
#ifdef L5
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	dhmp_malloc(size,3); //for L5

	show("start count",&task_time_start);

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

	show("over count",&task_time_end);

	for(i=0;i<objnum;i++)	
		dhmp_free(addr[i]);
#endif
#ifdef RFP
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	dhmp_malloc(size,6); //RFP

	show("start count",&task_time_start);

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

	show("over count",&task_time_end);

	for(i=0;i<objnum;i++)	
		dhmp_free(addr[i]);
#endif
#ifdef DaRPC
	//need DaRPC_SERVER at dhmp.h only for server
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	dhmp_malloc(0,5); //DaRPC
	char batch = 5;
	int k;
	uintptr_t* temp = malloc(batch*sizeof(uintptr_t));

	show("start count",&task_time_start);

	for(j=0;j<(accessnum /100 )* write_part;j=j+batch)
	{ 
		i = j%objnum;
		for(k = 0;k<batch;k++)
			temp[k] = (uintptr_t*)addr[rand_num[(i+k)%objnum]];
		model_3_DaRPC(size, str, temp, 1, batch); //用的默认的send recv queue
	}
	for(;j<accessnum;j=j+batch)
	{ 
		i = j%objnum;
		for(k = 0;k<batch;k++)
			temp[k] = (uintptr_t*)addr[rand_num[(i+k)%objnum]];
		model_3_DaRPC(size, str, temp, 0, batch); //用的默认的send recv queue
	}

	show("over count",&task_time_end);

	for(i=0;i<objnum;i++)	
		dhmp_free(addr[i]);
#endif
#ifdef Tailwind
	show("start count",&task_time_start);
	dhmp_malloc((size*100), 4); // for Tailwind
	model_6_Tailwind(accessnum,objnum, rand_num, size, str); // only unif ，用的默认的send recv queue 
	show("over count",&task_time_end); 
#endif



#ifdef scalable
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	dhmp_malloc(size,7); //scalable
	for(j=0;j<accessnum;j++)
	{ 
		i = j%objnum;
		model_7_scalable(accessnum, rand_num, size, str);
	}
	for(i=0;i<objnum;i++)	
		dhmp_free(addr[i]);
#endif
#ifdef FaSST
//need #define UD
	for(j=0;j<accessnum;j++)
	{ 
		send_UD(str,size);
	}
#endif


	
	task_time_diff_ns = ((task_time_end.tv_sec * 1000000000) + task_time_end.tv_nsec) -
                        ((task_time_start.tv_sec * 1000000000) + task_time_start.tv_nsec);
  	printf("%d %d runtime %lf ms\n",size, write_part, (double)task_time_diff_ns/1000000);

	dhmp_client_destroy();

	
	return 0;
}



