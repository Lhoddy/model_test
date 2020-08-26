#include <stdio.h>

#include "dhmp.h"
#define  obj_num_max  10005

const double A = 1.3;  
const double C = 1.0;  

double pf[obj_num_max]; 
int rand_num[obj_num_max]={0};

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
		objnum=10000;//atoi(argv[2]);
		accessnum = 100000;//atoi(argv[3]);
	}
	pick();
	str=malloc(size);
	if(!str){
		printf("alloc mem error");
		return -1;
	}
	memset(str, 0 , size);
	snprintf(str+8, size, "hello world hello");
	
	dhmp_client_init(size,objnum);
	dhmp_malloc(obj_num,2); // clover c&s point
	dhmp_malloc(size,3); //for L5
	
	for(i=0;i<objnum;i++)
	{
		addr[i]=dhmp_malloc(size,0);
	}
	fprintf(stderr,"start count");	
	fflush(stderr);
	clock_gettime(CLOCK_MONOTONIC, &task_time_start);	
	int j;
	for(j=0;j<accessnum;j++)
	{ 
		i = j%objnum;
		model_A_write(addr[rand_num[i]], size, str);	/*need FLUSH for single*/
		// model_A_writeImm(addr[rand_num[i]], size, str);
		// model_B_write(addr[rand_num[i]], size, str);   				/*need model_B*/
		// model_B_writeImm(addr[rand_num[i]], size, str);				
		// model_B_send(addr[rand_num[i]], size, str);
		// model_C_sread(addr[rand_num[i]], size, str);					/*need model_C*/
		// model_D_write(addr[rand_num[i]], size, str);	/*need FLUSH for single*/  
		// model_D_writeImm(addr[rand_num[i]], size, str);				

		// model_D_send(addr[i], size, str);

		model_1_octopus(addr[rand_num[i]], size, str);
		model_1_clover(addr[rand_num[i]], size, str);
		model_4_RFP(addr[rand_num[i]], size, str);
		model_5_L5(addr[rand_num[i]], size, str);
	}

	

	clock_gettime(CLOCK_MONOTONIC, &task_time_end);
	fprintf(stderr,"over count");
    fflush(stderr);
	for(i=0;i<objnum;i++)
	{
		dhmp_free(addr[i]);
	}
	dhmp_client_destroy();
	task_time_diff_ns = ((task_time_end.tv_sec * 1000000000) + task_time_end.tv_nsec) -
                        ((task_time_start.tv_sec * 1000000000) + task_time_start.tv_nsec);
  	printf("runtime %lf\n", (double)task_time_diff_ns/1000000);
	return 0;
}



