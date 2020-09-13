#include "dhmp_log.h"
#include "dhmp.h"
#define DEBUG_ON

enum dhmp_log_level global_log_level=DHMP_LOG_LEVEL_WARN;

const char *const level_str[]=
{
	"ERROR", "WARN", "INFO", "DEBUG","TRACE"
};
	
void dhmp_log_impl(const char * file, unsigned line, const char * func, unsigned log_level, const char * fmt, ...)
{
	va_list args;
	char mbuf[1024];
	int mlength=0;

	const char *short_filename;
	char filebuf[100];//filename at most 256
	
	/*get the args,fill in the string*/
	va_start(args,fmt);
	mlength=vsnprintf(mbuf,sizeof(mbuf),fmt,args);
	va_end(args);
	mbuf[mlength]=0;

	/*get short file name*/
	short_filename=strrchr(file,'/');
	short_filename=(!short_filename)?file:short_filename+1;


	snprintf(filebuf,sizeof(filebuf),"%s:%u",short_filename,line);

#ifdef DEBUG_ON
	printk(KERN_ALERT "%-28s [%-5s] - %s\n",filebuf,level_str[log_level], mbuf);
#endif
}




