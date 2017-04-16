#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sched.h>
#include <time.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <errno.h>

#include <iostream>
#include <cmath>
#include <cerrno>
#include <cstring>
#include <clocale>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <linux/socket.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <signal.h>
#include <net/if.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <netinet/udp.h>
#include <netinet/ip.h>
#include <linux/filter.h>
#include <sys/mman.h>
#include <poll.h>
#include <linux/net_tstamp.h>
#include <dlfcn.h>
#include "vma_extra.h"


#define MAX_PIDS_TS 256
#define MAX_SOCKETS_THREAD 		256
#define PRINT_PERIOD 			5000000

int (*vma_recvmmsg)(int, void *, int, int, void *);
int (*vma_recv)(int, void *, int, int);
int (*vma_socket)(int, int, int);
int (*vma_bind)(int, sockaddr *, int);
int (*vma_setsockopt)(int, int, int, void *, int);
int (*vma_ioctl)(int, int, void *);
int (*vma_close)(int);

int sock_num;
int threads_num;
int sleep_time;
unsigned int g_totalPacketsProcessed =0;

struct pesinfo {
	int		lastcc;
	int		rxDrop;
	uint64_t	rxCount;
};


typedef void (*validatePackets)(uint8_t*, size_t, struct RXSock*);
typedef void (*validatePacket)(uint8_t*, struct RXSock*);
typedef void (*printInfo)(struct RXSock*);


struct RXSock {
	uint64_t	rxCount;
	int		rxDrop;
	uint64_t	statTime;
	int		lastBlockId;
	int		LastSequenceNumber;
	int		lastPacketType;
	int		index;
	int		fd;
	int		ring_fd;
	uint16_t	rtpPayloadType;
	uint16_t	sin_port;
	struct ip_mreqn	mc;
	uint16_t	rPids[MAX_PIDS_TS];
	pesinfo		*pidTable;
	char		ipAddress[INET_ADDRSTRLEN];
	validatePacket	fvalidatePacket;
	validatePackets	fvalidatePackets;
	printInfo	fprintinfo;
};

struct RXThread {
	pthread_t	t;
	struct RXSock	*sock[MAX_SOCKETS_THREAD];
	int		sock_len;
	size_t		min_s;
	size_t		max_s;
};


static void checkpacket(uint8_t* data, struct RXSock* sock);
static void checkMpegTsPacket(uint8_t* data, struct RXSock* sock);
static void checkRtpPacket(uint8_t* data, struct RXSock* sock);
static void checkGVSPV2packet(uint8_t* data, struct RXSock* sock);

static void checkMpegTsPackets(uint8_t* data,size_t packets,struct RXSock* sock);
static void checkGVSPV2packets(uint8_t* data, size_t packets, struct RXSock* sock);
static void checkRtpPackets(uint8_t* data, size_t packets, struct RXSock* sock);
static void checkpackets(uint8_t* data, size_t packets, struct RXSock* sock);
static void reportErrorPacket(uint8_t* packet,struct RXSock* sock);
static void printdummyInfo(RXSock* sock);
static void printMpegTsInfo(RXSock* sock);
static inline void printRtpInfo(struct RXSock* sock);
static inline void printGvspInfo(struct RXSock* sock);



/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
static inline unsigned long long int time_get_usec()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (((unsigned long long int) tv.tv_sec * 1000000LL)
			+ (unsigned long long int) tv.tv_usec);
}

int scenario;
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
static int OpenRxSocket(struct sockaddr_in *addr, uint32_t ssm, char *device, struct ip_mreqn *mc)
{
	int i_ret;
	struct timeval timeout = { 0, 1 };
	int i_opt = 1;
	struct ifreq ifr;
	struct sockaddr_in *p_addr;


	// Create the socket
	int RxSocket = vma_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (RxSocket < 0) {
		printf("OpenRxSocket: Failed to create socket (%s)\n",
			std::strerror(errno));
		return 0;
	}

	// Enable socket reuse ( for multi channels bind to a single socket )
	i_ret = vma_setsockopt(RxSocket, SOL_SOCKET, SO_REUSEADDR,
			(void *) &i_opt, sizeof(i_opt));
	if (i_ret < 0) {
		vma_close(RxSocket);
		RxSocket = 0;
		printf("OpenRxSocket: Failed to set SO_REUSEADDR (%s)\n",
				strerror(errno));
		return 0;
	}
	fcntl(RxSocket, F_SETFL, O_NONBLOCK);
	// Set max socket recieve buffer
	/*i_ret = vma_setsockopt(RxSocket, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size,
	 sizeof(rcvbuf_size));
	 if (i_ret < 0) {
	 printf("OpenRxSocket: Failed to set SO_RCVBUF (%s)\n", strerror(errno));
	 vma_close(RxSocket);
	 RxSocket = 0;
	 return 0;
	 }*/

	{ // bind to specific device
		struct ifreq interface;
		strncpy(interface.ifr_ifrn.ifrn_name, device, IFNAMSIZ);
		//printf("OpenRxSocket SO_BINDTODEVICE %s\n",interface.ifr_ifrn.ifrn_name);
		if (vma_setsockopt(RxSocket, SOL_SOCKET, SO_BINDTODEVICE,
				(char *) &interface, sizeof(interface)) < 0) {
			printf("OpenRxSocket: Failed to bind to device (%s)\n",
					strerror(errno));
			vma_close(RxSocket);
			RxSocket = 0;
			return 0;
		}
	}
	// strideRQ
#if 0
	if (scenario == 2) {
		struct vma_api_t *vma_api = vma_get_api();
		if (vma_api == NULL) {
			printf("VMA Extra API not found - working with default socket APIs");
			exit(1);
		}
		vma_ring_type_attr ring;
		ring.ring_type = VMA_RING_CYCLIC_BUFFER;
		ring.ring_cyclicb.num = (1<<8);
		ring.ring_cyclicb.stride_bytes = 1400;
		ring.ring_cyclicb.comp_mask = 3;
		int p;
		int res = vma_api->vma_add_ring_profile(&ring, &p);
		if (res) {
			printf("failed adding ring profile");
			exit(-1);
		}
		vma_ring_alloc_logic_attr profile;
		profile.comp_mask = 13;
		profile.engress = 0;
		profile.ingress = 1;
		profile.ring_profile_key = p;
		profile.ring_alloc_logic = RING_LOGIC_PER_SOCKET;
		vma_setsockopt(RxSocket, SOL_SOCKET, SO_VMA_RING_ALLOC_LOGIC,
				&profile, sizeof(profile));
	}
#endif
	// bind to socket
	i_ret = vma_bind(RxSocket, (struct sockaddr *)addr, sizeof(struct sockaddr));
	if (i_ret < 0) {
		printf("OpenRxSocket: Failed to bind to socket (%s)\n",
				strerror(errno));
		vma_close(RxSocket);
		RxSocket = 0;
		return 0;
	}

	memset(&ifr, 0, sizeof(struct ifreq));
	strncpy(ifr.ifr_name, device, IFNAMSIZ);
	// Get device IP
	i_ret = ioctl(RxSocket, SIOCGIFADDR, &ifr);
	if (i_ret < 0) {
		printf("OpenRxSocket: Failed to obtain interface IP (%s)\n",
				strerror(errno));
		vma_close(RxSocket);
		RxSocket = 0;
		return 0;
	}

	if (((addr->sin_addr.s_addr & 0xFF) >= 224) && ((addr->sin_addr.s_addr & 0xFF) <= 239)) {
		p_addr = (struct sockaddr_in *) &(ifr.ifr_addr);
		if (ssm == 0) {
			struct ip_mreqn mreq;
			// join the multicast group on specific device
			memset(&mreq, 0, sizeof(struct ip_mreqn));

			mreq.imr_multiaddr.s_addr = addr->sin_addr.s_addr;
			mreq.imr_address.s_addr = p_addr->sin_addr.s_addr;
			*mc = mreq;
			// RAFI MP_RING is created
			i_ret = vma_setsockopt(RxSocket, IPPROTO_IP,
					IP_ADD_MEMBERSHIP, &mreq,
					sizeof(struct ip_mreqn));

			if (i_ret < 0) {
				printf("OpenRxSocket: add membership to (0X%08X) on (0X%08X) failed. (%s)\n",
					mreq.imr_multiaddr.s_addr,
					mreq.imr_address.s_addr,
					strerror(errno));
				vma_close(RxSocket);
				RxSocket = 0;
				return 0;
			}
		} else {
			struct ip_mreq_source mreqs;
			// join the multicast group on specific device
			memset(&mreqs, 0, sizeof(struct ip_mreq_source));

			mreqs.imr_multiaddr.s_addr = addr->sin_addr.s_addr;
			mreqs.imr_interface.s_addr = p_addr->sin_addr.s_addr;
			mreqs.imr_sourceaddr.s_addr = ssm;

			i_ret = setsockopt(RxSocket, IPPROTO_IP,
					IP_ADD_SOURCE_MEMBERSHIP, &mreqs,
					sizeof(struct ip_mreq_source));

			if (i_ret < 0) {
				printf("OpenRxSocket: add membership to (0X%08X), ssm (0X%08X) failed. (%s)\n",
					mreqs.imr_multiaddr.s_addr,
					mreqs.imr_sourceaddr.s_addr,
					strerror(errno));
				vma_close(RxSocket);
				RxSocket = 0;
				return 0;
			}
		}
	}

	// Set max receive timeout
	i_ret = vma_setsockopt(RxSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout,
			sizeof(struct timeval));
	if (i_ret < 0) {
		printf("OpenRxSocket: Failed to set SO_RCVTIMEO (%s)\n",
				strerror(errno));
		vma_close(RxSocket);
		RxSocket = 0;
		return 0;
	}
	return RxSocket;
}

void *run_copy(void *arg)
{
	struct RXThread *t = (struct RXThread *) arg;
	uint8_t Dump[2048];
	printf("starting rx\n");
	while (1) {
		for (int i = 0; i < t->sock_len; i++) {
			for (int j = 0; j < 10; j++) {
				int size = vma_recv(t->sock[i]->fd, Dump, 2048,
						MSG_NOSIGNAL);
				if (size > 0)
					t->sock[i]->fvalidatePacket(Dump, t->sock[i]);
			}
			uint64_t currentTime = time_get_usec();
			if (currentTime > t->sock[i]->statTime) {
				t->sock[i]->fprintinfo(t->sock[i]);
				t->sock[i]->statTime = currentTime + PRINT_PERIOD;
			}
		}
		usleep(sleep_time);
	}
	return NULL;
}

void *run_stride(void *arg)
{
	struct RXThread *t = (struct RXThread *) arg;
	uint8_t *data;
	struct vma_api_t *vma_api = vma_get_api();
	int flags = 0;
	if (vma_api == NULL) {
		printf("VMA Extra API not found - working with default socket APIs");
		exit(1);
	}
	for (int i = 0; i < t->sock_len; i++) {
		int ring_fd_num = vma_api->get_socket_rings_num(t->sock[i]->fd);
		int* ring_fds = new int[ring_fd_num];
		vma_api->get_socket_rings_fds(t->sock[i]->fd, ring_fds,
				ring_fd_num);
		t->sock[i]->ring_fd = *ring_fds;
		delete[] ring_fds;
	}
	flags = MSG_DONTWAIT;
	printf("starting rx\n");
	struct vma_completion_mp_t completion;
	for (int iter = 0; iter < 1000000; iter++) {
		for (int i = 0; i < t->sock_len; i++) {
			for (int j = 0; j < 10; j++) {
				completion.packets = 0;
				flags = MSG_DONTWAIT;
				int res = vma_api->vma_cyclic_buffer_read(
						t->sock[i]->ring_fd,
						&completion, t->min_s, t->max_s,
						&flags);
				if (res == -1) {
					printf("vma_cyclic_buffer_read returned -1");
					exit(-1);
				}
				if (completion.packets == 0) {
					continue;
				}
				data = ((uint8_t *) completion.payload_ptr);
				printf("run_stride: parsing %d packets\n",(int)completion.packets);
				t->sock[i]->fvalidatePackets(data, completion.packets, t->sock[i]);
			}
		}
		usleep(sleep_time);
	}
	//leave MC
	for (int i = 0; i < t->sock_len; i++) {
		int rc = vma_setsockopt(t->sock[i]->fd, IPPROTO_IP,
		IP_DROP_MEMBERSHIP, &t->sock[i]->mc, sizeof(struct ip_mreqn));
		if (rc < 0) {
			printf("OpenRxSocket: drop add membership to (0X%08X) on (0X%08X) failed. (%s)\n",
				t->sock[i]->mc.imr_multiaddr.s_addr,
				t->sock[i]->mc.imr_address.s_addr,
				strerror(errno));
			vma_close(t->sock[i]->fd);
		}
	}
	return NULL;
}

void *run_zero(void *arg)
{
	struct RXThread *t = (struct RXThread *) arg;
	uint8_t Dump[2048];
	uint8_t *data;
	struct vma_api_t *vma_api = vma_get_api();
	int flags = 0;
	if (vma_api == NULL) {
		printf("VMA Extra API not found - working with default socket APIs");
		exit(1);
	}
	printf("starting rx\n");
	while (1) {
		for (int i = 0; i < t->sock_len; i++) {
			for (int j = 0; j < 10; j++) {
				int size = vma_api->recvfrom_zcopy(
						t->sock[i]->fd, &Dump, 2048,
						&flags, NULL, NULL);
				if (MSG_VMA_ZCOPY & flags)
					data = (uint8_t *) ((struct vma_packets_t*) Dump)->pkts[0].iov[0].iov_base;
				else
					data = Dump;
				if (size > 0 && ((data[0] & 0xC0) == 0x80)
						&& ((data[1] & 0x7f) == 0x62)) {
					t->sock[i]->fvalidatePacket(data, t->sock[i]);


					if (MSG_VMA_ZCOPY & flags) {
						vma_api->free_packets(t->sock[i]->fd,
								((struct vma_packets_t*) Dump)->pkts,
								((struct vma_packets_t*) Dump)->n_packet_num);
					}
				} else {
					uint64_t currentTime = time_get_usec();
					if (currentTime	> t->sock[i]->statTime)
					t->sock[i]->fprintinfo(t->sock[i]);
				}
			}
		}
		usleep(sleep_time);
	}
	return NULL;
}
const char* get_sceanrio_str(int scen)
{
	switch (scen) {
	case 0:
		return "default VMA";
	case 1:
		return "zero copy VMA";
	case 2:
		return "cyclic Buffer VMA";
	default:
		return "ERROR";
	}
}

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
int main(int argc, char *argv[])
{

	printf("-------------------------------------------------------------\n");
	printf("streamCaptureDemo                                            \n");
	printf("-------------------------------------------------------------\n");
	struct RXSock fds[1024];
	struct RXThread rxThreads[MAX_SOCKETS_THREAD];

	if (argc < 3) {
		printf("usage: streamCaptureDemo eth0 [file of ip port] fds_num threads_num sceanrio [0,1,2] "
			"sleep [min packet] [max packet] use_vma\n");
		printf("   logs packet drops\n");
		printf("   \n");
		exit(-1);
	}

	std::ifstream infile(argv[2]);
	std::vector<struct sockaddr_in> ip_vect;
	std::string ip;
	int port;
	while (infile >> ip >> port)
	{
		struct sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		addr.sin_addr.s_addr = inet_addr(ip.c_str());
		addr.sin_addr.s_addr = ntohl(ntohl(addr.sin_addr.s_addr ));
		printf("adding port %s port %d,\n",ip.c_str(),port);
		if (addr.sin_addr.s_addr < 0x01000001) {
			printf("Error - illegal IP %x\n",
					addr.sin_addr.s_addr);
			exit(-1);
		}
		ip_vect.push_back(addr);
	}
	if (argc > 3) {
		sock_num = atoi(argv[3]);
	}
	if (sock_num != (int)ip_vect.size()) {
		printf("ip list given but not the same as sock_num using first %d\n",
			sock_num);
	}
	if (argc > 4) {
		threads_num = atoi(argv[4]);
	}
	scenario = 2;
	if (argc > 5) {
		scenario = atoi(argv[5]);
	}
	if (argc > 6) {
		sleep_time = atoi(argv[6]);
	} else {
		sleep_time = 1;
	}
	int min_s = 500;
	if (argc > 7) {
		min_s = atoi(argv[7]);
	}
	int max_s = 5000;
	if (argc > 8) {
		max_s = atoi(argv[8]);
	}
	printf("running checker with:\n\tfds: %d\n \tthreads:%d "
		"\n\tscenario: %s \n\tmin packet %d\n\tmax packet %d "
		"\n\tsleep_time %d\n",
		sock_num, threads_num, get_sceanrio_str(scenario),
		min_s, max_s, sleep_time);
	int use_vma = 1;
	if (argc > 9) {
		use_vma = atoi(argv[9]);
	}
	//load VMA so
	if (sock_num < threads_num) {
		printf("Error - you need to have at least the same thread as sockets. "
			"threads %d sockets %d\n", threads_num, sock_num);
		exit(-1);
	}
	void *handle = dlopen("libvma.so", RTLD_NOW | RTLD_GLOBAL);
	if (handle && use_vma) {
		printf("using VMA shared libary\n");
		*(void **) &vma_socket = dlsym(handle, "socket");
		*(void **) &vma_bind = dlsym(handle, "bind");
		*(void **) &vma_recvmmsg = dlsym(handle, "recvmmsg");
		*(void **) &vma_recv = dlsym(handle, "recv");
		*(void **) &vma_setsockopt = dlsym(handle, "setsockopt");
		*(void **) &vma_ioctl = dlsym(handle, "ioctl");
		*(void **) &vma_close = dlsym(handle, "close");
		dlerror();
	} else {
		printf("not using VMA shared libary\n");
		*(void **) &vma_socket = dlsym(0, "socket");
		*(void **) &vma_bind = dlsym(0, "bind");
		*(void **) &vma_recvmmsg = dlsym(0, "recvmmsg");
		*(void **) &vma_recv = dlsym(0, "recv");
		*(void **) &vma_setsockopt = dlsym(0, "setsockopt");
		*(void **) &vma_ioctl = dlsym(0, "ioctl");
		*(void **) &vma_close = dlsym(0, "close");
	}
	for (int i = 0; i < sock_num; i++) {
		struct ip_mreqn mc;
		fds[i].fd = OpenRxSocket(&ip_vect[i], 0, argv[1], &mc);
		if (fds[i].fd <= 0) {
			printf("Error - rx open failed. %d\n", i);
			exit(-1);
		}
		memcpy(&fds[i].mc, &mc, sizeof(mc));
		fds[i].LastSequenceNumber = -1;
		fds[i].lastBlockId = -1;
		fds[i].rxCount = 0;
		fds[i].rxDrop = 0;
		fds[i].statTime = time_get_usec() + 1000*i;
		fds[i].index = i;
		fds[i].fvalidatePacket = checkpacket;
		fds[i].fvalidatePackets = checkpackets;
		fds[i].fprintinfo = printdummyInfo;

		fds[i].sin_port = ntohs(ip_vect[i].sin_port);
		inet_ntop(AF_INET, &(ip_vect[i].sin_addr), fds[i].ipAddress, INET_ADDRSTRLEN);
		for (int j = 0; j < MAX_PIDS_TS ; j++) {
			fds[i].rPids[j] = 0x1FFF;
			}
		fds[i].pidTable = new pesinfo[8192];
		for (int j = 0; j < 8192; j++) {
			fds[i].pidTable[j].lastcc = -1;
			fds[i].pidTable[j].rxDrop = 0;
			fds[i].pidTable[j].rxCount =0;

		}
	}
	// Distribute fds to threads
	for (int var = 0; var < threads_num; ++var) {
		rxThreads[var].sock_len = 0;
	}
	for (int var = 0; var < sock_num; ++var) {
		int thread_id = var % threads_num;
		rxThreads[thread_id].sock[rxThreads[thread_id].sock_len] =
				&fds[var];
		rxThreads[thread_id].sock_len++;
		rxThreads[thread_id].max_s = max_s;
		rxThreads[thread_id].min_s = min_s;
	}

	for (int i = 0; i < threads_num; i++) {
		switch (scenario) {
		case 0:
			if (pthread_create(&rxThreads[i].t, NULL, run_copy, &rxThreads[i])) {
				fprintf(stderr, "error creating thread\n");
				return 1;
			}
			break;
		case 1:
			if (pthread_create(&rxThreads[i].t, NULL, run_zero, &rxThreads[i])) {
				fprintf(stderr, "error creating thread\n");
				return 1;
			}
			break;
		case 2:
			if (pthread_create(&rxThreads[i].t, NULL, run_stride, &rxThreads[i])) {
				fprintf(stderr, "error creating thread\n");
				return 1;
			}
			break;
		default:
			printf("bad scenario valid is 0-2 got %d\n", scenario);
			exit(-1);
			break;
		}
	}
	for (int i = 0; i < threads_num; i++) {
		if (pthread_join(rxThreads[i].t, NULL)) {
			fprintf(stderr, "Error creating thread\n");
			return 1;
		}
	}
	for (int i = 0; i < sock_num; i++) {
		delete[] fds[i].pidTable;
	}
	exit(0);
}



static inline void checkRtpPacket(uint8_t* data, struct RXSock* sock)
{
	uint32_t rtpHeader;
	// version == 2 and payload type (PT) is  98 – High bit rate media transport / 27-MHz Clock
	if (((data[0] & 0xC0) == 0x80) && ((data[1] & 0x7f) == sock->rtpPayloadType)) {
		sock->rxCount++;
		g_totalPacketsProcessed++;
		rtpHeader = htonl((uint32_t) (*(uint32_t*) (data)));
		uint16_t SequenceNumber = (uint16_t) (rtpHeader & 0xFFFF);
		int LostCount = (SequenceNumber - (uint16_t) sock->LastSequenceNumber - 1);
		//if (LostCount > 0)
		//  printf("check RTP lostcount %d packet sn %u last sn %d\n",LostCount,SequenceNumber,sock->LastSequenceNumber);
		if (sock->LastSequenceNumber >= 0)
			sock->rxDrop += LostCount;
		sock->LastSequenceNumber = SequenceNumber;
	} else {
		reportErrorPacket(data, sock);
	}
}


void checkRtpPackets(uint8_t* data, size_t packets, struct RXSock* sock)
{
	data += 42;
	for (size_t k = 0; k < packets; k++) {
		checkRtpPacket(data,sock);
		// skip to the end of the stride
		data += 2048;
	}
	uint64_t currentTime = time_get_usec();
	if (currentTime > sock->statTime) {
		printRtpInfo(sock);
		sock->statTime = currentTime + PRINT_PERIOD;
	}
}

void checkpacket(uint8_t* data, struct RXSock* sock)
{
	uint8_t* pdata = data;
	bool isMPEGTS = true;
	bool isRtp = false;
	printf("%s data = 0x%lx\n",__func__,(unsigned long)data);
	// check if this is mpeg2 TS
	g_totalPacketsProcessed++;
	for (int pes = 0; pes < 7; pes++, pdata += 188) {
		if (0x47 != *pdata){
			// this is not MPEG TS...
			isMPEGTS = false;
			break;
		}
	}
	if (isMPEGTS) {
		printf("Socket address %s:%u, found MPEG Ts packets, will be parsed as Mpeg Ts\n",
			sock->ipAddress, sock->sin_port);
		sock->fvalidatePacket = checkMpegTsPacket;
		sock->fvalidatePackets = checkMpegTsPackets;
		sock->fprintinfo = printMpegTsInfo;
		return;
	}
	pdata = data;
	// version == 2 and payload type (PT) is  98 – High bit rate media transport / 27-MHz Clock
	if ((pdata[0] & 0xC0) == 0x80) {
		uint16_t payloadType = (pdata[1] & 0x7F);
		sock->rtpPayloadType = payloadType;
		switch (payloadType){
		case 0x62: //98
			printf("socket address %s:%u, Found RTP packet with raw video\n",sock->ipAddress,sock->sin_port);
			break;
		case 33:
			printf("socket address %s:%u, Found RTP packet Mpeg2 TS\n",sock->ipAddress,sock->sin_port);
			break;
		case 26:
			printf("socket address %s:%u, Found RTP packet with JPEG\n",sock->ipAddress,sock->sin_port);
			break;
		default:
			printf("socket address %s:%u, Found RTP packet with payload Type %d\n",sock->ipAddress,sock->sin_port,sock->rtpPayloadType);
		}
		isRtp = true;
	}
	if (isRtp) {
		sock->fvalidatePacket = checkRtpPacket;
		sock->fvalidatePackets = checkRtpPackets;
		sock->fprintinfo = printRtpInfo;
		return;
	}
	if ((pdata[0]== 0 ) && ( pdata[1] ==0 )) {
		sock->fvalidatePacket = checkGVSPV2packet;
		sock->fvalidatePackets = checkGVSPV2packets;
		sock->fprintinfo = printGvspInfo;
		printf("Socket address %s:%u, will be parsed ad GVSP (default) format\n", sock->ipAddress,sock->sin_port);
	} else {
		printf("failed to parse packet, retry\n");
	}
}

void checkpackets(uint8_t* data, size_t packets, struct RXSock* sock)
{
	checkpacket(data +42, sock);
	return sock->fvalidatePackets(data, packets, sock);
}

static inline void checkMpegTsPacket(uint8_t* data, RXSock* sock)
{
	uint16_t pid;
	// skip mac IP and UDP hdr
	printf("%s data = 0x%lx\n", __func__, (unsigned long) data);
	g_totalPacketsProcessed++;
	sock->rxCount++;
	for (int pes = 0; pes < 7; pes++, data += 188) {
		uint32_t tsheader = htonl(*((uint32_t *) data));
		pid = (uint16_t) ((tsheader & 0x1FFF00) >> 8);
		if (0x47 == *data) {
			if (pid != 0x1FFF) {
				if (sock->pidTable[pid].lastcc >= 0) {
					uint32_t adaptationF = (tsheader & 0x30);
					sock->pidTable[pid].rxCount++;
					if ((adaptationF != 0x20) && (adaptationF != 0x0)) {
						sock->pidTable[pid].lastcc = ((sock->pidTable[pid].lastcc  + 1) & 0xF);
						if ((0 == (sock->pidTable[pid].lastcc & 0xF)) && (0 != (tsheader & 0xF))) {
							sock->pidTable[pid].rxDrop += 1;
							sock->rxDrop++;
							sock->pidTable[pid].lastcc = (tsheader & 0xF);
						}
					}
				} else { // first time
					sock->pidTable[pid].lastcc = (tsheader & 0xF);
					for (int p = 0; p < MAX_PIDS_TS; p++) {
						if (sock->rPids[p] == 0x1FFF) {
							sock->rPids[p] = pid;
							printf("\n<%s:%u>:(pes-%d): Adding new pid to DB, in index %d, pid 0x%x\n",
								sock->ipAddress,
								sock->sin_port,
								pes, p, pid);
							break;
						}
					}
				}
			}
		} else {
			if (pes != 0) {
				printf("error TS packet pes = %d\n", pes);
			}
			uint8_t* temp = data - 42;
			reportErrorPacket(temp, sock);
		}
	}
}

void reportErrorPacket(uint8_t* packet, struct RXSock* sock)
{
	unsigned long laddress = (unsigned long) packet;
	printf("packet %u at address 0x%lx found to be bad\n",
		g_totalPacketsProcessed, laddress);
	printf("Packet dump 4KB \n");
	unsigned int* datal = (unsigned int*) packet;
	for (int k = 0; k < 4; k++) {
		for (int j = 0; j < 32; j++) {
			for (int i = 0; i < 8; i++) {
				printf("0x%x ", datal[i]);
			}
			printf("\n");
			datal += 8;
		}
		printf("\n\n\n");
	}
	exit(32);
}

static void printdummyInfo(RXSock* sock)
{

}

static inline void printRtpInfo(struct RXSock* sock)
{
	printf("<%s:%u>: received %d packets, %d drops\n", sock->ipAddress,sock->sin_port,(int) sock->rxCount,(int) sock->rxDrop);
	sock->rxCount = 0;
	sock->rxDrop = 0;
}
static inline void printMpegTsInfo(RXSock* sock)
{
	if (sock->rxDrop > 0) {
		printf("<%s:%u>: <PID>: <recieved> <CC ERRORS>\n",
			sock->ipAddress, sock->sin_port);
		for (int p = 0; p < MAX_PIDS_TS; p++) {
			int pid = sock->rPids[p];
			if (pid != 0x1FFF) {
				printf("\t0x%x:,%lu %d\n", (int) pid,
					sock->pidTable[pid].rxCount,
					sock->pidTable[pid].rxDrop);
				sock->pidTable[pid].rxDrop = 0;
				sock->pidTable[pid].rxCount = 0;
			}
		}
		sock->rxDrop = 0;
	}
}

void checkMpegTsPackets(uint8_t* data, size_t packets, RXSock* sock)
{
	data += 42;
	for (size_t k = 0; k < packets; k++) {
		checkMpegTsPacket(data, sock);
		data += 2048;
	}
	unsigned long long currentTime = time_get_usec();
	if (currentTime > sock->statTime) {
		printf("check cc errors\n");
		printMpegTsInfo(sock);
		sock->statTime = currentTime + PRINT_PERIOD;
	}
}


#define GVSP_PT_INIT -1
#define GVSP_PT_TRAILER 2
#define GVSP_PT_LEADER  1
#define GVSP_PT_PAYLOAD 3

static inline void printGvspInfo(RXSock* sock)
{
	printf("GVSP<%s:%u>: received %d packets, %d drops\n", sock->ipAddress,sock->sin_port,(int) sock->rxCount,(int) sock->rxDrop);
	sock->rxCount = 0;
	sock->rxDrop = 0;
}


void checkGVSPV2packets(uint8_t* data, size_t packets, RXSock* sock)
{
	data += 42;
	for (size_t k = 0; k < packets; k++) {
		checkGVSPV2packet(data,  sock);
		data += 8192;
	}
	unsigned long long currentTime = time_get_usec();
	if (currentTime > sock->statTime) {
		printGvspInfo(sock);
		printf("check cc errors\n");
		sock->statTime = currentTime + PRINT_PERIOD;
	}
}

void checkGVSPV2packet(uint8_t* data, struct RXSock* sock)
{
	uint32_t lastPacketId = sock->LastSequenceNumber;
	int lastPacketType = sock->lastPacketType;
	uint32_t status_block_id = htonl(*((uint32_t *) data));
	uint32_t e_packet_id = htonl(*((uint32_t *) &data[4]));
	uint32_t status = status_block_id >> 16;
	uint32_t block_id = status_block_id = 0xFFFF;
	uint32_t packet_id = e_packet_id & 0xFFFFFF;
	int packet_type = ((e_packet_id & 0x0F000000) >> 24);
	sock->rxCount++;
	if (status != 0x0) {
		printf("GVSP found bad status %u\n", status);
		return;
	}

	if (lastPacketType == GVSP_PT_PAYLOAD) {
		//block id should not increment and packet id should be increment in one packet type can be payload or trailer
		if (block_id != (uint32_t) sock->lastBlockId) {
			printf("GVSP lost block(s) [%d -%d]\n", block_id,
					sock->lastBlockId);
			sock->rxDrop += 1000000;
		}
		if ((packet_id != lastPacketId + 1) &&
		    (packet_id != ((lastPacketId + 2) & 0xFFFFFF))) {
			sock->rxDrop += (packet_id - lastPacketId);
		}
		sock->lastBlockId = (int) block_id;
		sock->LastSequenceNumber = packet_id;
		sock->lastPacketType = packet_type;
	} else if (lastPacketType == GVSP_PT_INIT) {
		// this is the first packet captured
		sock->lastBlockId = (int) block_id;
		sock->LastSequenceNumber = packet_id;
		sock->lastPacketType = packet_type;
		printf("GVSP: init parsing found block %u packet %u type %d\n",
			block_id, packet_id, packet_type);
	} else if (lastPacketType == GVSP_PT_TRAILER) {
		// current packet must be leader, block id should be incremented in 1, packet id must be zero
		if (packet_type != GVSP_PT_LEADER) {
			printf("lost leader packet\n");
			sock->rxDrop++;
		}
		sock->lastBlockId = (int) block_id;
		sock->LastSequenceNumber = packet_id;
		sock->lastPacketType = packet_type;

	} else {
		// LEADER
		// block id should not increment , packet id should be increment in one and packet type must be payload
		if (block_id != (uint32_t) sock->lastBlockId) {
			printf("GVSP, expect first payload in pblock, but lost block(s) [%u -%u]\n",
				block_id, sock->lastBlockId);
			sock->rxDrop += 1000000;
		}
		if (packet_id != 1) {
			sock->rxDrop += packet_id - 1;
		}
		sock->lastBlockId = (int) block_id;
		sock->LastSequenceNumber = packet_id;
		sock->lastPacketType = packet_type;
	}
}

