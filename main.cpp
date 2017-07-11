#include<stdio.h>
#include<string.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<stdlib.h>
#include<getopt.h>
#include <unistd.h>
#include<errno.h>

#include <fcntl.h>
//#include"aes.h"

#include <sys/epoll.h>
#include <sys/wait.h>

#include<map>
#include<string>
#include<vector>


#include <sys/socket.h>    //for socket ofcourse
#include <sys/types.h>
#include <stdlib.h> //for exit(0);
#include <errno.h> //For errno - the error number
#include <netinet/tcp.h>   //Provides declarations for tcp header
#include <netinet/ip.h>    //Provides declarations for ip header
#include <netinet/if_ether.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <byteswap.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/filter.h>

#include <sys/time.h>
#include <time.h>

#include <sys/timerfd.h>

using namespace std;

char local_address[100], remote_address[100],source_address[100];
int local_port = -1, remote_port = -1,source_port;

const int buf_len = 20480;

const int server_mode=2;
const int client_mode=1;
int prog_mode=0; //0 unset; 1client 2server
const int RETRY_TIME=3;

void handler(int num) {
	int status;
	int pid;
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		if (WIFEXITED(status)) {
			//printf("The child exit with code %d",WEXITSTATUS(status));
		}
	}
}

void setnonblocking(int sock) {
	int opts;
	opts = fcntl(sock, F_GETFL);

	if (opts < 0) {
		perror("fcntl(sock,GETFL)");
		exit(1);
	}
	opts = opts | O_NONBLOCK;
	if (fcntl(sock, F_SETFL, opts) < 0) {
		perror("fcntl(sock,SETFL,opts)");
		exit(1);
	}

}

int udp_fd;
int raw_recv_fd;
int raw_send_fd;
int init_raw_socket()
{
	raw_send_fd = socket(AF_INET , SOCK_RAW , IPPROTO_TCP);
    if(raw_send_fd == -1) {
        perror("Failed to create raw_send_fd");
        exit(1);
    }
	//raw_fd = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_ALL));

	raw_recv_fd= socket(PF_PACKET, SOCK_RAW, htons(ETH_P_IP));
	//raw_fd=socket(AF_PACKET , SOCK_RAW , htons(ETH_P_IP));
    // packet_recv_sd = socket(AF_INET , SOCK_RAW , IPPROTO_TCP);
    if(raw_recv_fd == -1) {
        //socket creation failed, may be because of non-root privileges
        perror("Failed to create raw_recv_fd");
        exit(1);
    }

    //IP_HDRINCL to tell the kernel that headers are included in the packet

    int one = 1;
    const int *val = &one;
    if (setsockopt (raw_send_fd, IPPROTO_IP, IP_HDRINCL, val, sizeof (one)) < 0) {
        perror("Error setting IP_HDRINCL");
        exit(2);
    }
	return 0;
}
char buf[buf_len];
char buf2[buf_len];
char raw_send_buf[buf_len];
char raw_send_buf2[buf_len];
struct sock_filter code[] = {
		{ 0x28, 0, 0, 0x0000000c },//0
		{ 0x15, 0, 10, 0x00000800 },//1
		{ 0x30, 0, 0, 0x00000017 },//2
		{ 0x15, 0, 8, 0x00000006 },//3
		{ 0x28, 0, 0, 0x00000014 },//4
		{ 0x45, 6, 0, 0x00001fff },//5
		{ 0xb1, 0, 0, 0x0000000e },//6
		{ 0x48, 0, 0, 0x0000000e },//7
		{ 0x15, 2, 0, 0x0000ef32 },//8
		{ 0x48, 0, 0, 0x00000010 },//9
		{ 0x15, 0, 1, 0x0000ef32 },//10
		{ 0x6, 0, 0, 0x0000ffff },//11
		{ 0x6, 0, 0, 0x00000000 },//12
};
sock_fprog bpf;
long long get_current_time()
{
	timespec tmp_time;
	clock_gettime(CLOCK_MONOTONIC, &tmp_time);
	return tmp_time.tv_sec*1000+tmp_time.tv_nsec/(1000*1000ll);
}
void init_filter(int port)
{
	code[8].k=code[10].k=port;
	bpf.len = sizeof(code)/sizeof(code[0]);
	bpf.filter = code;
	//printf("<%d>\n",bpf.len);
	int ret = setsockopt(raw_recv_fd, SOL_SOCKET, SO_ATTACH_FILTER, &bpf, sizeof(bpf));
	memset(code,0,sizeof(code));
	if (ret != 0)
	{
		printf("error set fiter\n");
		exit(-1);
	}
}
void process_arg(int argc, char *argv[])
{
	int i,j,k,opt;
    static struct option long_options[] =
      {
        /* These options set a flag. */
        {"source", required_argument,    0, 1},
      };
    int option_index = 0;
	printf("argc=%d ", argc);
	for (i = 0; i < argc; i++)
		printf("%s ", argv[i]);
	printf("\n");

	if (argc == 1)
	{
		printf(
				"proc -l [adress:]port -r [adress:]port  [-a passwd] [-b passwd]\n");
		exit(-1);
	}

	int no_l = 1, no_r = 1;
	while ((opt = getopt_long(argc, argv, "l:r:sch",long_options,&option_index)) != -1) {
		//string opt_key;
		//opt_key+=opt;
		switch (opt) {
		case 'l':
			no_l = 0;
			if (strchr(optarg, ':') != 0) {
				sscanf(optarg, "%[^:]:%d", local_address, &local_port);
			} else {
				strcpy(local_address, "127.0.0.1");
				sscanf(optarg, "%d", &local_port);
			}
			break;
		case 'r':
			no_r = 0;
			if (strchr(optarg, ':') != 0) {
				sscanf(optarg, "%[^:]:%d", remote_address, &remote_port);
			} else {
				strcpy(remote_address, "127.0.0.1");
				sscanf(optarg, "%d", &remote_port);
			}
			break;
		case 's':
			if(prog_mode==0)
			{
				prog_mode=server_mode;
			}
			else
			{
				printf("-s /-c has already been set,-s option conflict");
				exit(-1);
			}
			break;
		case 'c':
			if(prog_mode==0)
			{
				prog_mode=client_mode;
			}
			else
			{
				printf("-s /-c has already been set,-c option conflict");
				exit(-1);
			}
			break;
		case 'h':
			break;
		case 1:
			if (strchr(optarg, ':') != 0) {
				sscanf(optarg, "%[^:]:%d", source_address, &source_port);
				printf("source: %s:%d",source_address,&source_port);
			} else {
				printf("format --source ip:adress");
				exit(-1);
			}
			break;

		default:
			printf("ignore unknown <%s>", optopt);
		}
	}

	if (no_l)
		printf("error: -i not found\n");
	if (no_r)
		printf("error: -o not found\n");
	if(prog_mode==0)
		printf("error: -s /-r  hasnt been set\n");
	if (no_l || no_r||prog_mode==0)
	{
		exit(-1);
	}
}

int recv_raw()
{
}
struct packet_info_t
{
	//ip_part:
	uint32_t src_ip;
	uint16_t src_port;
	uint32_t dst_ip;
	uint16_t dst_port;

	//tcp_part:
	bool syn,ack,psh;
	uint32_t seq,ack_seq;


}g_packet_info;

struct pseudo_header {
    u_int32_t source_address;
    u_int32_t dest_address;
    u_int8_t placeholder;
    u_int8_t protocol;
    u_int16_t tcp_length;
};
/*
    Generic checksum calculation function
*/
unsigned short csum(unsigned short *ptr,int nbytes) {
    register long sum;
    unsigned short oddbyte;
    register short answer;

    sum=0;
    while(nbytes>1) {
        sum+=*ptr++;
        nbytes-=2;
    }
    if(nbytes==1) {
        oddbyte=0;
        *((u_char*)&oddbyte)=*(u_char*)ptr;
        sum+=oddbyte;
    }

    sum = (sum>>16)+(sum & 0xffff);
    sum = sum + (sum>>16);
    answer=(short)~sum;

    return(answer);
}

int send_raw(packet_info_t &info,char * payload,int payloadlen)
{
	char *data;

    memset(raw_send_buf,0,payloadlen+100);

    struct iphdr *iph = (struct iphdr *) raw_send_buf;

    //TCP header
    struct tcphdr *tcph = (struct tcphdr *) (raw_send_buf + sizeof (struct ip));
    struct sockaddr_in sin;
    struct pseudo_header psh;

    //Data part
    data = raw_send_buf + sizeof(struct iphdr) + sizeof(struct tcphdr);

    memcpy(data , payload, payloadlen);

    //some address resolution
    sin.sin_family = AF_INET;
    sin.sin_port = htons(info.dst_port);
    sin.sin_addr.s_addr = info.dst_ip;

    //Fill in the IP Header
    iph->ihl = 5;
    iph->version = 4;
    iph->tos = 0;
    iph->tot_len = sizeof (struct iphdr) + sizeof (struct tcphdr) + payloadlen;
    iph->id = htonl (54321); //Id of this packet
    iph->frag_off = 0;
    iph->ttl = 255;
    iph->protocol = IPPROTO_TCP;
    iph->check = 0; //Set to 0 before calculating checksum
    iph->saddr = info.src_ip;    //Spoof the source ip address
    iph->daddr = info.dst_ip;

    //TCP Header
    tcph->source = htons(info.src_port);
    tcph->dest = htons(info.dst_port);

    tcph->seq =htonl(info.seq);
    tcph->ack_seq = htonl(info.ack);

    tcph->doff = 5;  //tcp header size
    tcph->fin=0;
    tcph->syn=info.syn;
    tcph->rst=0;
    tcph->psh=info.psh;
    tcph->ack=info.ack;

    tcph->urg=0;
    tcph->window = htons((uint16_t)129600);
    tcph->check = 0; //leave checksum 0 now, filled later by pseudo header
    tcph->urg_ptr = 0;

    psh.source_address = info.src_ip;
    psh.dest_address = sin.sin_addr.s_addr;
    psh.placeholder = 0;
    psh.protocol = IPPROTO_TCP;
    psh.tcp_length = htons(sizeof(struct tcphdr) + payloadlen );

    int psize = sizeof(struct pseudo_header) + sizeof(struct tcphdr) + payloadlen;

     memcpy(raw_send_buf2 , (char*) &psh , sizeof (struct pseudo_header));
     memcpy(raw_send_buf2 + sizeof(struct pseudo_header) , tcph , sizeof(struct tcphdr) + payloadlen);

     tcph->check = csum( (unsigned short*) raw_send_buf2, psize);

     //Ip checksum
     iph->check = csum ((unsigned short *) raw_send_buf, iph->tot_len);

     int ret = sendto(raw_send_fd, raw_send_buf, iph->tot_len ,  0, (struct sockaddr *) &sin, sizeof (sin));
	 printf("<ret:%d>\n",ret);
	 if(ret<0)
     {

    	 perror("raw send error");
    	 //printf("send error\n");
     }
     return 0;
}
int send_sync()
{
	g_packet_info.seq=0;
	g_packet_info.ack=0;
	g_packet_info.syn=1;
	g_packet_info.ack_seq=0;
	g_packet_info.psh=0;
	send_raw(g_packet_info,0,0);
	return 0;
}
const int client_nothing=0;
const int client_syn_sent=1;
const int client_ack_sent=2;
const int client_ready=3;
int client_current_state=client_nothing;
int retry_counter;

long long last_state_time;
uint32_t session_id;

uint32_t get_true_random_number()
{
	uint32_t ret;
	int fd=open("/dev/urandom",O_RDONLY);
	read(fd,&ret,sizeof(ret));
	return htonl(ret);
}
const int server_nothing=0;
const int server_syn_ack_sent=1;
const int server_heartbeat_sent=2;
const int server_ready=3;
int server_current_state=server_nothing;
long long last_hb_recv_time;
int fake_tcp_keep_connection_client() //for client
{
	//printf("timer!");
	//fflush(stdout);
	begin:
	if(client_current_state==client_nothing)
	{
		send_sync(/*sync*/);//send sync
		client_current_state=client_syn_sent;
		last_state_time=get_current_time();
		printf("state changed from nothing to syn_sent\n");
		retry_counter=RETRY_TIME;

	}
	if(client_current_state==client_syn_sent  &&get_current_time()-last_state_time>1000ll)
	{
		if(retry_counter==0)
		{
			client_current_state=client_nothing;
			goto begin;
		}
		else
		{
			retry_counter--;
			printf("retry send sync\n");
			send_sync(/*sync*/);//send sync again
			last_state_time=get_current_time();
		}
	}
	if(client_current_state==client_ack_sent &&get_current_time()-last_state_time>1000)
	{
		printf("!!!!!\n");
		fflush(stdout);
		if(retry_counter==0)
		{
			client_current_state=client_nothing;
			goto begin;
		}
		else
		{
			retry_counter--;
			send_raw(g_packet_info,0,0);
			last_state_time=get_current_time();
			printf("retry send ack  counter left:%d\n",retry_counter);
			fflush(stdout);
		}
	}

	if(client_current_state==client_ready)
	{
		printf("time %lld %lld\n",get_current_time(),last_state_time);
		if(get_current_time()-last_hb_recv_time>5*1000ll)
		{
			client_current_state=client_nothing;
			return 0;
		}
		g_packet_info.syn=0;
		g_packet_info.ack=1;
		printf("heartbeat sent\n");
		send_raw(g_packet_info,"hb",strlen("hb"));
		//last_time=get_current_time();
	}

}

int fake_tcp_keep_connection_server()
{
	//begin:
	printf("timerxxxx!\n");
	fflush(stdout);
	if(server_current_state==server_nothing)
	{
		return 0;
	}
	if(server_current_state==server_syn_ack_sent &&get_current_time()-last_state_time>1000ll )
	{
		if(retry_counter==0)
		{
			server_current_state=server_nothing;
			//send_raw(g_packet_info,0,0);
		}
		else
		{
			retry_counter--;
			send_raw(g_packet_info,0,0);
			last_state_time=get_current_time();
			printf("resend syn ack\n");
		}
		//send_raw(/*syn ack*/);
	}
	if(server_current_state==server_heartbeat_sent||server_current_state==server_ready)
	{
		if(get_current_time()-last_hb_recv_time>5*1000ll)
		{
			printf("%lld %lld",get_current_time(),last_state_time);
			server_current_state=server_nothing;
			printf("changed state to server_nothing111\n");
			return 0;
		}

		g_packet_info.syn=0;
		g_packet_info.ack=1;
		g_packet_info.psh=1;
		send_raw(g_packet_info,"hb",strlen("hb"));
		//last_time=get_current_time();
		printf("heart beat sent,fffff\n");
		fflush(stdout);
	}

}
struct sockaddr_in udp_old_addr_in;



int set_timer(int epollfd,int &timer_fd)
{
	int ret;
	epoll_event ev;

	itimerspec its;
	memset(&its,0,sizeof(its));

	if((timer_fd=timerfd_create(CLOCK_MONOTONIC,TFD_NONBLOCK)) < 0)
	{
		printf("timer_fd create error");
		exit(1);
	}
	its.it_interval.tv_sec=1;
	its.it_value.tv_sec=1;
	timerfd_settime(timer_fd,0,&its,0);


	ev.events = EPOLLIN;
	ev.data.fd = timer_fd;

	epoll_ctl(epollfd, EPOLL_CTL_ADD, timer_fd, &ev);
	if (ret < 0) {
		printf("epoll_ctl return %d\n", ret);
		exit(-1);
	}
}

const int MTU=1440;
int client_recv(iphdr * iph,tcphdr *tcph,char * data,int data_len)
{

	if(client_current_state==client_syn_sent )
	{
		if (!(tcph->syn==1&&tcph->ack==1)) return 0;
		if(iph->saddr!=g_packet_info.dst_ip||ntohs(tcph->source)!=g_packet_info.dst_port)
		{
			printf("unexpected adress %d %d  %d %d\n",iph->saddr,g_packet_info.dst_ip,ntohl(tcph->source),g_packet_info.dst_port);
			return 0;
		}

		g_packet_info.ack_seq=ntohl(tcph->ack_seq)+1;
		g_packet_info.psh=0;
		g_packet_info.syn=0;
		g_packet_info.ack=1;
		g_packet_info.seq+=1;

		printf("sent ack back\n");
		fflush(stdout);
		send_raw(g_packet_info,0,0);
		client_current_state=client_ack_sent;
		printf("changed state to client_ack_sent\n");
		last_state_time=get_current_time();
		retry_counter=RETRY_TIME;
	}
	if(client_current_state==client_ack_sent )
	{
		//printf(" i m here\n");
		//fflush(stdout);
		if( tcph->syn==1||tcph->ack!=1 )
		{
			printf("unexpected syn ack");
			return 0;
		}
		if(iph->saddr!=g_packet_info.dst_ip||ntohs(tcph->source)!=g_packet_info.dst_port)
		{
			printf("unexpected adress\n");
			return 0;
		}
		client_current_state=client_ready;
		printf("changed state to client_ready\n");
		//send_raw(g_packet_info,"hb",strlen("hb"));
	}
	if(client_current_state==client_ready )
	{
		printf("heart beat received\n");
		last_hb_recv_time=get_current_time();
	}
}
int send_heart_beat(packet_info_t &info)
{

}
int server_recv(iphdr * iph,tcphdr *tcph,char * data,int data_len)
{
	if(server_current_state==server_nothing)
	{
		if(!( tcph->syn==1&&tcph->ack==0 )) return 0;

		g_packet_info.dst_port=ntohs(tcph->source);
		g_packet_info.dst_ip=iph->saddr;

		g_packet_info.ack_seq=ntohl(tcph->ack_seq)+1;
		g_packet_info.psh=0;
		g_packet_info.syn=1;
		g_packet_info.ack=1;
		g_packet_info.seq;//dont need to set
		printf("sent syn ack\n");
		fflush(stdout);
		send_raw(g_packet_info,0,0);
		server_current_state=server_syn_ack_sent;
		printf("changed state to server_syn_ack_sent\n");
		retry_counter=RETRY_TIME;
		last_state_time=get_current_time();
	}
	else if(server_current_state==server_syn_ack_sent)
	{
		if(!( tcph->syn==0&&tcph->ack==1 )) return 0;


		server_current_state=server_heartbeat_sent;
		g_packet_info.syn=0;
		g_packet_info.ack=1;

		send_raw(g_packet_info,"hb",strlen("hb"));
		printf("changed state to server_heartbeat_sent\n");

		last_hb_recv_time=get_current_time(); //this ack is counted as hearbeat
		last_state_time=get_current_time();
	}
	else if(server_current_state==server_heartbeat_sent)//heart beat received
	{
		if( tcph->syn==1||tcph->ack!=1 )  return 0;
		if(iph->saddr!=g_packet_info.dst_ip||ntohs(tcph->source)!=g_packet_info.dst_port)
		{
			printf("unexpected adress\n");
			return 0;
		}
		server_current_state=server_ready;
		last_state_time=get_current_time();
		last_hb_recv_time=get_current_time();
		printf("changed state to server_ready\n");

	}
	else if(server_current_state==server_ready)
	{
		if( tcph->syn==1||tcph->ack!=1 )  return 0;
		if(iph->saddr!=g_packet_info.dst_ip||ntohs(tcph->source)!=g_packet_info.dst_port)
		{
			printf("unexpected adress\n");
			return 0;
		}
		printf("heart beat received\n");
		last_hb_recv_time=get_current_time();
	}
}


int on_raw_recv()
{
	int size;
	struct sockaddr saddr;
	socklen_t saddr_size;
	saddr_size = sizeof(saddr);

	printf("raw!\n");
	fflush(stdout);

	size = recvfrom(raw_recv_fd, buf, buf_len, 0 ,&saddr , &saddr_size);

	char *ip_data=buf+14;

	struct iphdr *iph = (struct iphdr *) (ip_data);


    if (!(iph->ihl > 0 && iph->ihl < (MTU)/4)) {
    	printf("iph ihl error");
    	fflush(stdout);
        return 0;
    }

    if (iph->protocol != IPPROTO_TCP) {
    	printf("iph protocal != tcp\n");
    	fflush(stdout);
    	return 0;
    }


	int ip_len=ntohs(iph->tot_len);

    unsigned short iphdrlen =iph->ihl*4;
    struct tcphdr *tcph=(struct tcphdr*)(ip_data+ iphdrlen);
    unsigned short tcphdrlen = tcph->doff*4;

    if (!(tcph->doff > 0 && tcph->doff < (MTU - iphdrlen)/4)) {
    	printf("tcph error");
    	fflush(stdout);
    	return 0;
    }

    int data_len = ip_len-tcphdrlen-iphdrlen;

    char *data=ip_data+tcphdrlen+iphdrlen;

    //printf("%d\n",ip_len);
    /*
    for(int i=0;i<size;i++)
    {
    	printf("<%x>",(unsigned char)buf[i]);

    }
	  printf("\n");

    for(int i=0;i<data_len;i++)
    {
    	printf("<%x>",(unsigned char)data[i]);
    }*/

    printf("\n");
	printf("<%u,%u,%u,%u,%d>\n",(unsigned int)iphdrlen,(unsigned int)tcphdrlen,(unsigned int)tcph->syn,(unsigned int)tcph->ack,data_len);
	fflush(stdout);

	if(prog_mode==server_mode)
	{
		server_recv(iph,tcph,data,data_len);
	}
	else
	{
		client_recv(iph,tcph,data,data_len);
	}
	return 0;
}
int client()
{
	int i, j, k;int ret;
	init_raw_socket();
	session_id=get_true_random_number();

	init_filter(source_port);
	g_packet_info.dst_ip=inet_addr(remote_address);
	g_packet_info.dst_port=remote_port;

	g_packet_info.src_ip=inet_addr(source_address);
	g_packet_info.src_port=source_port;

	int udp_fd=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	int yes = 1;
	//setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	struct sockaddr_in local_me;

	socklen_t slen = sizeof(sockaddr_in);
	memset(&local_me, 0, sizeof(local_me));
	local_me.sin_family = AF_INET;
	local_me.sin_port = htons(local_port);
	local_me.sin_addr.s_addr = inet_addr(local_address);
	if (bind(udp_fd, (struct sockaddr*) &local_me, slen) == -1) {
		perror("socket bind error");
		exit(1);
	}
	setnonblocking(udp_fd);
	int epollfd = epoll_create1(0);
	const int max_events = 4096;
	struct epoll_event ev, events[max_events];
	if (epollfd < 0) {
		printf("epoll return %d\n", epollfd);
		exit(-1);
	}
	ev.events = EPOLLIN;
	ev.data.fd = udp_fd;
	ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, udp_fd, &ev);
	if (ret!=0) {
		printf("add  udp_listen_fd error\n");
		exit(-1);
	}
	ev.events = EPOLLIN;
	ev.data.fd = raw_recv_fd;

	ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, raw_recv_fd, &ev);
	if (ret!= 0) {
		printf("add raw_fd error\n");
		exit(-1);
	}

	////add_timer for fake_tcp_keep_connection_client

	//sleep(10);

	memset(&udp_old_addr_in,0,sizeof(sockaddr_in));
	int unbind=1;
	int timer_fd;

	set_timer(epollfd,timer_fd);
	while(1)////////////////////////
	{
		int nfds = epoll_wait(epollfd, events, max_events, 180 * 1000);
		if (nfds < 0) {  //allow zero
			printf("epoll_wait return %d\n", nfds);
			exit(-1);
		}
		int n;
		for (n = 0; n < nfds; ++n) {
			if (events[n].data.fd == raw_recv_fd)
			{
				on_raw_recv();
				/*if(is_sync_ack)
				{

				}
				else if(is heart_beat)
				{

				}
				else if(is_data)
				{
					sendto();
				}*/
			}
			if(events[n].data.fd ==timer_fd)
			{
				//printf("timer!\n");
				//fflush(stdout);
				uint64_t value;
				read(timer_fd, &value, 8);
				fake_tcp_keep_connection_client();
			}
			if (events[n].data.fd == udp_fd)
			{
				socklen_t recv_len;
				struct sockaddr_in udp_new_addr_in;
				if ((recv_len = recvfrom(udp_fd, buf, buf_len, 0,
						(struct sockaddr *) &udp_new_addr_in, &slen)) == -1) {
					printf("recv_from error");
					exit(1);
				}
				printf("Received packet from %s:%d\n", inet_ntoa(udp_new_addr_in.sin_addr),
						ntohs(udp_new_addr_in.sin_port));
				fflush(stdout);
				if(udp_old_addr_in.sin_addr.s_addr==0&&udp_old_addr_in.sin_port==0)
				{
					memcpy(&udp_old_addr_in,&udp_new_addr_in,sizeof(udp_new_addr_in));
				}
				else if(udp_new_addr_in.sin_addr.s_addr!=udp_old_addr_in.sin_addr.s_addr
						||udp_new_addr_in.sin_port!=udp_old_addr_in.sin_port)
				{
					printf("new <ip,port> connected in,ignore\n");
					fflush(stdout);
					continue;
				}
				////send_data_raw(buf,recv_len);
			}
		}
	}
	return 0;
}
int server()
{
	int i, j, k;int ret;
	init_raw_socket();
	init_filter(local_port);
	int epollfd = epoll_create1(0);
	const int max_events = 4096;

	g_packet_info.src_ip=inet_addr(local_address);
	g_packet_info.src_port=local_port;

	struct epoll_event ev, events[max_events];
	if (epollfd < 0) {
		printf("epoll return %d\n", epollfd);
		exit(-1);
	}

	ev.events = EPOLLIN;
	ev.data.fd = raw_recv_fd;

	ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, raw_recv_fd, &ev);
	if (ret!= 0) {
		printf("add raw_fd error\n");
		exit(-1);
	}
	int timer_fd;
	set_timer(epollfd,timer_fd);
	while(1)////////////////////////
	{
		int nfds = epoll_wait(epollfd, events, max_events, 180 * 1000);
		if (nfds < 0) {  //allow zero
			printf("epoll_wait return %d\n", nfds);
			exit(-1);
		}
		int n;
		const int MTU=1440;
		for (n = 0; n < nfds; ++n)
		{
			printf("%d %d %d %d\n",timer_fd,raw_recv_fd,raw_send_fd,n);
			if (events[n].data.fd == timer_fd)
			{
				uint64_t value;
				read(timer_fd, &value, 8);
				fake_tcp_keep_connection_server();
			}
			if (events[n].data.fd == raw_recv_fd)
			{
				on_raw_recv();
			}
		}
	}
	return 0;
}
int main(int argc, char *argv[])
{
	int i, j, k;

	signal(SIGCHLD, handler);
	process_arg(argc,argv);

	if(prog_mode==client_mode)
	{
		client();
	}
	else
	{
		server();
	}

	return 0;
}
