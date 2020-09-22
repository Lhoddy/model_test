#include <stdio.h>

#include "dhmp.h"
#define  obj_num_max  1005

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
	void* addr[obj_num_max];
	int i,size,rnum;
	char *str;
	int objnum,accessnum;
	struct timespec task_time_start, task_time_end;
	unsigned long task_time_diff_ns;
	
	if(argc<2)
	{
		printf("input param error. input:<filname> <size> <objnum> <accessnum>\n");
		printf("note:max of <objnum> = max of <accessnum> == 10000\n");
		return -1;
	}
	else
	{
		size=atoi(argv[1]);
		objnum=1000;//atoi(argv[2]);
		accessnum = 10000;//atoi(argv[3]);
	}
	pick();
	str=malloc(size);
	if(!str){
		printf("alloc mem error");
		return -1;
	}
	memset(str, 0 , size);
	snprintf(str+8, size-8, "hello world hello");
	
	dhmp_client_init(size,objnum);

#ifdef octopus
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);
	int j;
	for(j=0;j<accessnum;j++)
	{ 
		i = j%objnum;
		model_1_octopus(addr[rand_num[i]], size, str);
	}
	for(i=0;i<objnum;i++)	
		dhmp_free(addr[i]);
#endif
#ifdef clover
	dhmp_malloc(objnum,2); // clover c&s point
	for(i=0;i<objnum;i++)
		addr[i]=dhmp_malloc(size,0);1
	for(j=0;j<accessnum;j++)
	{ 
		i = j%objnum;
		model_1_clover(addr[i], size, str, addr[rand_num[i]]);
	}
	for(i=0;i<objnum;i++)
		dhmp_free(addr[i]);
#endif
#ifdef L5
	dhmp_malloc(size,3); //for L5
	for(j=0;j<accessnum;j++)
	{ 
		i = j%objnum;
		model_5_L5(size, str);
	}
#endif

#ifdef DaRPC
	dhmp_malloc(0,5); //DaRPC
	model_3_DaRPC(accessnum, rand_num, size, str ); //用的默认的send recv queue
	
#endif
#ifdef scalable
	dhmp_malloc(size,7); //scalable
	model_7_scalable(accessnum, rand_num, size, str);
	
#endif
#ifdef RFP
	dhmp_malloc(size,6); //RFP
	for(j=0;j<accessnum;j++)
	{ 
		model_4_RFP(size, str);  
	}
#endif
#ifdef FaSST
//need #define UD
	for(j=0;j<accessnum;j++)
	{ 
		send_UD(str,size);
	}
#endif

#ifdef Tailwind
	dhmp_malloc((size*objnum), 4); // for Tailwind
	model_6_Tailwind(accessnum,objnum, rand_num, size, str); // only unif ，用的默认的send recv queue  
#endif
	
	show("start count",&task_time_start);
	show("over count",&task_time_end);
	

	dhmp_client_destroy();
	task_time_diff_ns = ((task_time_end.tv_sec * 1000000000) + task_time_end.tv_nsec) -
                        ((task_time_start.tv_sec * 1000000000) + task_time_start.tv_nsec);
  	printf("runtime %lf ms\n", (double)task_time_diff_ns/1000000);
	return 0;
}



