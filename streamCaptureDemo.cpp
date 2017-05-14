#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sched.h>
#include <time.h>
#include <set>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/resource.h>
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
#define PRINT_PERIOD_SEC		5
#define PRINT_PERIOD 			1000000 * PRINT_PERIOD_SEC
#define MAX_RINGS 1000
#define MAX_SOCKETS_PER_RING 4096
#define STRIDE_SIZE				2048

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

class RXSock;
class CommonCyclicRing;


typedef void (*validatePackets)(uint8_t*, size_t, CommonCyclicRing*);
typedef void (*validatePacket)(uint8_t*, RXSock*);
typedef void (*printInfo)(RXSock*);
typedef void* (*Process_func)(void*);

Process_func fp_process =NULL;

class RXSock {
public:
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
	int bad_packets;
	validatePacket	fvalidatePacket;
	printInfo	fprintinfo;
  RXSock(){ pidTable = new pesinfo[8192]; }
  virtual ~RXSock() { delete[] pidTable; }
  };
struct flow_param {
  int ring_id;
  unsigned short hash;
  sockaddr_in addr;
};


class CommonCyclicRing {
  public:
    int numOfSockets;
    int ring_id;
	int ring_fd;
    RXSock* hashedSock[MAX_SOCKETS_PER_RING];
    std::vector<sockaddr_in*> addr_vect;
    std::vector<RXSock*> sock_vect;
	validatePackets	fvalidatePackets;
    CommonCyclicRing():numOfSockets(0),ring_fd(0){
    for (int i=0; i < MAX_SOCKETS_PER_RING; i++ ) {
      hashedSock[i] = 0;
    	}
	
    	}
	void PrintInfo();
};

void CommonCyclicRing::PrintInfo()
{
	int bad_packets =0,packetCount=0, packetDrop=0, dead_sockets=0;
	for(int i=0; i< numOfSockets; i++) {
		bad_packets += sock_vect[i]->bad_packets;
		sock_vect[i]->bad_packets=0;
    int count = sock_vect[i]->rxCount;
    if ( count == 0 )
      dead_sockets++;
    else
  		packetCount += count;
		sock_vect[i]->rxCount=0;
		packetDrop += sock_vect[i]->rxDrop;
		sock_vect[i]->rxDrop=0;
		}
	printf("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	printf("| rng id| sockets | dead sock | Packets  | drop     | bad      |\n");
	printf("| %02d\t| %04d    |  %04d   | %08d | %08d | %08d |\n", ring_id,numOfSockets,dead_sockets,packetCount,packetDrop,bad_packets);
	printf("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
}

class RXThread {
public:
  RXThread():numOfRings(0){}
	pthread_t	t;
  std::vector<CommonCyclicRing*> rings;
  int numOfRings;
//	RXSock	*sock[MAX_SOCKETS_THREAD];
	int		sock_len;
	size_t		min_s;
	size_t		max_s;
};


static unsigned short hashIpPort2(sockaddr_in addr );
static inline unsigned short getHashValFromPacket(uint8_t* data);


static void checkpacket(uint8_t* data, RXSock* sock);
static void checkMpegTsPacket(uint8_t* data, RXSock* sock);
static void checkRtpPacket(uint8_t* data, RXSock* sock);
static void checkGVSPV2packet(uint8_t* data, RXSock* sock);
/*
static void checkMpegTsPackets(uint8_t* data,size_t packets,CommonCyclicRing* pRing);
static void checkGVSPV2packets(uint8_t* data, size_t packets, CommonCyclicRing* pRing);
static void checkRtpPackets(uint8_t* data, size_t packets, CommonCyclicRing* pRing);
*/
static void CheckSingleSocketPackets(uint8_t* data, size_t packets, CommonCyclicRing* pRing);
static void CheckMultiSocketsPackets(uint8_t* data, size_t packets, CommonCyclicRing* pRing);
	

static void printdummyInfo(RXSock* sock);
static void printMpegTsInfo(RXSock* sock);
static inline void printRtpInfo(RXSock* sock);
static inline void printGvspInfo(RXSock* sock);



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

static int CreateRingProfile(bool CommonFdPerRing, int RingProfile, int user_id, int RxSocket )
{
  vma_ring_alloc_logic_attr profile;
  profile.engress = 0;
  profile.ingress = 1;
  profile.ring_profile_key = RingProfile;
  if (CommonFdPerRing ) {	
		profile.user_id =user_id;
		profile.comp_mask = VMA_RING_ALLOC_MASK_RING_PROFILE_KEY|
							VMA_RING_ALLOC_MASK_RING_USER_ID	  |
							VMA_RING_ALLOC_MASK_RING_INGRESS;

    // if we want several Fd's per ring, we need to assign RING_LOGIC_PER_THREAD / RING_LOGIC_PER_CORE
		profile.ring_alloc_logic = RING_LOGIC_PER_USER_ID;
  }
  else {
		profile.comp_mask = VMA_RING_ALLOC_MASK_RING_PROFILE_KEY|
							VMA_RING_ALLOC_MASK_RING_INGRESS;

    // if we want several Fd's per ring, we need to assign RING_LOGIC_PER_THREAD / RING_LOGIC_PER_CORE
		profile.ring_alloc_logic = RING_LOGIC_PER_SOCKET;
  }
  return vma_setsockopt(RxSocket, SOL_SOCKET, SO_VMA_RING_ALLOC_LOGIC,&profile, sizeof(profile));
}


/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
static int OpenRxSocket(int ring_id, sockaddr_in* addr, uint32_t ssm, char *device, 
						struct ip_mreqn *mc, int RingProfile, bool CommonFdPerRing)
{
	int i_ret;
	struct timeval timeout = { 0, 1 };
	int i_opt = 1;
	struct ifreq ifr;
	struct sockaddr_in *p_addr;


	// Create the socket
	int RxSocket = vma_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (RxSocket < 0) {
		printf("%s: Failed to create socket (%s)\n",
			__func__,std::strerror(errno));
		return 0;
	}

	// Enable socket reuse ( for multi channels bind to a single socket )
	i_ret = vma_setsockopt(RxSocket, SOL_SOCKET, SO_REUSEADDR,
			(void *) &i_opt, sizeof(i_opt));
	if (i_ret < 0) {
		vma_close(RxSocket);
		RxSocket = 0;
		printf("%s: Failed to set SO_REUSEADDR (%s)\n",
				__func__,strerror(errno));
		return 0;
	}
	fcntl(RxSocket, F_SETFL, O_NONBLOCK);
	/* Set max socket recieve buffer
#define    IP_RCV_BUFFER_MAX_SIZE   (8096*4096)
	uint32_t rcvbuf_size = IP_RCV_BUFFER_MAX_SIZE;
	i_ret = vma_setsockopt(RxSocket, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size,
	 sizeof(rcvbuf_size));
	 if (i_ret < 0) {
	 printf("%s: Failed to set SO_RCVBUF (%s)\n",__func__, strerror(errno));
	 vma_close(RxSocket);
	 RxSocket = 0;
	 return 0;
	 }*/
	if (scenario == 2)
		CreateRingProfile(CommonFdPerRing, RingProfile, ring_id, RxSocket );
	 // bind to specific device
	struct ifreq interface;
	strncpy(interface.ifr_ifrn.ifrn_name, device, IFNAMSIZ);
	//printf("%s SO_BINDTODEVICE %s\n",__func__,interface.ifr_ifrn.ifrn_name);
	if (vma_setsockopt(RxSocket, SOL_SOCKET, SO_BINDTODEVICE,
			(char *) &interface, sizeof(interface)) < 0) {
		printf("%s: Failed to bind to device (%s)\n",
				__func__, strerror(errno));
		vma_close(RxSocket);
		RxSocket = 0;
		return 0;
	}


	// bind to socket
	i_ret = vma_bind(RxSocket, (struct sockaddr *)addr, sizeof(struct sockaddr));
	if (i_ret < 0) {
		printf("%s: Failed to bind to socket (%s)\n",__func__,strerror(errno));
		vma_close(RxSocket);
		RxSocket = 0;
		return 0;
	}

	memset(&ifr, 0, sizeof(struct ifreq));
	strncpy(ifr.ifr_name, device, IFNAMSIZ);
	// Get device IP
	i_ret = ioctl(RxSocket, SIOCGIFADDR, &ifr);
	if (i_ret < 0) {
		printf("%s: Failed to obtain interface IP (%s)\n",__func__,	strerror(errno));
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
				printf("%s: add membership to (0X%08X) on (0X%08X) failed. (%s)\n",__func__,mreq.imr_multiaddr.s_addr,
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
				printf("%s: add membership to (0X%08X), ssm (0X%08X) failed. (%s)\n",__func__,
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
		printf("%s: Failed to set SO_RCVTIMEO (%s)\n",__func__,
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
	uint8_t Dump[STRIDE_SIZE];
	printf("starting rx\n");
	while (1) {
		for (int r = 0; r < t->numOfRings; r++) {
			for (int i = 0; i < t->rings[r]->numOfSockets; i++) {
				RXSock* pSock = t->rings[r]->sock_vect[i];
				for (int j = 0; j < 1000; j++) {
					int size = vma_recv(pSock->fd, Dump, STRIDE_SIZE, MSG_NOSIGNAL);
					if (size > 0)
						pSock->fvalidatePacket(Dump, pSock);
					}
				pSock->fprintinfo(pSock);
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

	for (std::vector<CommonCyclicRing*>::iterator pRing = t->rings.begin();
			pRing != t->rings.end(); ++pRing) {
		int sock_len = (*pRing)->numOfSockets;
		for (int i = 0; i < sock_len; i++) {
			int ring_fd_num = vma_api->get_socket_rings_num((*pRing)->sock_vect[i]->fd);
			int* ring_fds = new int[ring_fd_num];
			vma_api->get_socket_rings_fds((*pRing)->sock_vect[i]->fd, ring_fds, ring_fd_num);
			(*pRing)->sock_vect[i]->ring_fd = *ring_fds;
			(*pRing)->ring_fd = *ring_fds;
			delete[] ring_fds;
		}
	}
	flags = MSG_DONTWAIT;
	printf("starting rx\n");
	struct vma_completion_cb_t completion;
  printf("number of rings = %d\n",t->numOfRings);
	for (int iter = 0; iter < 1000000; iter++) {
		for (int i = 0; i < t->numOfRings; i++) {
			for (int j = 0; j < 10; j++) {
				completion.packets = 0;
				flags = MSG_DONTWAIT;
				int res = vma_api->vma_cyclic_buffer_read(t->rings[i]->ring_fd,
						&completion, t->min_s, t->max_s, flags);
				if (res == -1) {
					printf("vma_cyclic_buffer_read returned -1");
					exit(-1);
				}

				if (completion.packets == 0) {
					continue;
				}
				data = ((uint8_t *) completion.payload_ptr);
				// printf("run_stride: parsing %d packets\n",(int)completion.packets);
				t->rings[i]->fvalidatePackets(data, completion.packets,t->rings[i]);
			}
		}
		usleep(sleep_time);
	}
	//leave MC
	for (int r = 0; r < t->numOfRings; r++) {
		for (int i = 0; i < t->rings[r]->numOfSockets; i++) {
			int rc = vma_setsockopt(t->rings[r]->sock_vect[i]->fd, IPPROTO_IP,
			IP_DROP_MEMBERSHIP, &t->rings[r]->sock_vect[i]->mc,
					sizeof(struct ip_mreqn));
			if (rc < 0) {
				printf(
						"%s: drop add membership to (0X%08X) on (0X%08X) failed. (%s)\n",
						__func__,
						t->rings[r]->sock_vect[i]->mc.imr_multiaddr.s_addr,
						t->rings[r]->sock_vect[i]->mc.imr_address.s_addr,
						strerror(errno));
				vma_close(t->rings[r]->sock_vect[i]->fd);
			}
		}
	}
	return NULL;
}

void *run_zero(void *arg)
{
	struct RXThread *t = (struct RXThread *) arg;
	uint8_t Dump[STRIDE_SIZE];
	uint8_t *data;
	struct vma_api_t *vma_api = vma_get_api();
	int flags = 0;
	if (vma_api == NULL) {
		printf("VMA Extra API not found - working with default socket APIs");
		exit(1);
	}
	printf("starting rx\n");
	while (1) {
		for (int r = 0; r < t->numOfRings; r++) {
			for (int i = 0; i < t->rings[r]->numOfSockets; i++) {
				RXSock* pSock = t->rings[r]->sock_vect[i];
				for (int j = 0; j < 1000; j++) {
					int size = vma_api->recvfrom_zcopy(pSock->fd, &Dump, STRIDE_SIZE,&flags, NULL, NULL);
					if (MSG_VMA_ZCOPY & flags)
						data = (uint8_t *) ((struct vma_packets_t*) Dump)->pkts[0].iov[0].iov_base;
					else
						data = Dump;
					if (size > 0) {
						pSock->fvalidatePacket(data, pSock);
						if (MSG_VMA_ZCOPY & flags) {
							vma_api->free_packets(pSock->fd,((struct vma_packets_t*) Dump)->pkts,((struct vma_packets_t*) Dump)->n_packet_num);
						}
					} else {
						pSock->fprintinfo(pSock);
					}
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

void AddFlow(flow_param flow,CommonCyclicRing* rings[], int &uniqueRings)
{
  int ring_id = flow.ring_id;
  if ( rings[ring_id] == NULL ) {
    rings[ring_id] = new CommonCyclicRing;
    rings[ring_id]->ring_id =ring_id;
    uniqueRings++;
  }
  rings[ring_id]->numOfSockets++;
  sockaddr_in* pAddr = new sockaddr_in;
  *pAddr = flow.addr;
  rings[ring_id]->addr_vect.push_back(pAddr);
}

void destroyFlows(CommonCyclicRing* rings[])
{
  for ( int i=0; i < MAX_RINGS; i++ ) {
    if (rings[i] != NULL ) {
	  for (std::vector<sockaddr_in*>::iterator it = rings[i]->addr_vect.begin(); it!=rings[i]->addr_vect.end(); ++it) {
        delete *it;
      }
      for (std::vector<RXSock*>::iterator it = rings[i]->sock_vect.begin(); it!=rings[i]->sock_vect.end(); ++it) {
        delete *it;
      }
      delete rings[i];
    }
  }
}
void ShowLimit()
{
  rlimit lim;
  int err = getrlimit(RLIMIT_NOFILE,&lim);
  printf("%d limit: %1ld,%1ld\n",err,lim.rlim_cur,lim.rlim_max);
}

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
int main(int argc, char *argv[])
{
  int hash_colision_cnt=0;
  ShowLimit();
  rlimit lim;
  lim.rlim_cur=100000;
  lim.rlim_max=100000;
  int err = setrlimit(RLIMIT_NOFILE,&lim);
  printf("set rlimit returned %d\n",err);
  ShowLimit();

	printf("-------------------------------------------------------------\n");
	printf("streamCaptureDemo                                            \n");
	printf("-------------------------------------------------------------\n");
	//struct RXSock fds[1024];
	CommonCyclicRing* pRings[MAX_RINGS];
  char HashColision[MAX_RINGS][MAX_SOCKETS_PER_RING] ={0};
	int uniqueRings;
	struct RXThread rxThreads[MAX_SOCKETS_THREAD];
	for (int j = 0; j < MAX_RINGS; j++) {
		pRings[j] = NULL;
	}
	if (argc < 3) {
		printf(
				"usage: streamCaptureDemo eth0 [file of ip port] fds_num threads_num sceanrio [0,1,2] "
						"sleep [min packet] [max packet] use_vma\n");
		printf("   logs packet drops\n");
		printf("   \n");
		exit(-1);
	}
	bool ringPerFd = false;
	std::ifstream infile(argv[2]);

	std::string ip;
	std::string line;
	int port;
	int ring_id;
	int lineNum = 0;
	int sock_num, socketRead = 0;

	if (argc > 3) {
		sock_num = atoi(argv[3]);
	}

	while (std::getline(infile, line)) {
		if ((line[0] == '#') || ((line[0] == '/') && (line[1] == '/'))) {
			continue;
		}
		std::istringstream iss(line);
		struct flow_param flow;
		if (iss >> ip >> port) {
			socketRead++;
			flow.addr.sin_family = AF_INET;
			flow.addr.sin_port = htons(port);
			flow.addr.sin_addr.s_addr = inet_addr(ip.c_str());
			flow.addr.sin_addr.s_addr = ntohl(ntohl(flow.addr.sin_addr.s_addr));
			//printf("adding ip %s port %d,\n", ip.c_str(), port);
			flow.hash = hashIpPort2(flow.addr);
			printf("adding %s:%d hash val %d\n",ip.c_str(),port, flow.hash);
			if (flow.addr.sin_addr.s_addr < 0x01000001) {
				printf("Error - illegal IP %x\n", flow.addr.sin_addr.s_addr);
				exit(-1);
			}
		} else {
			continue;
		}
		if (iss >> ring_id) {
		} else {
			printf("no common rings\n");
			ring_id = lineNum;
			lineNum++;
			ringPerFd = true;
		}
    printf("ring_id = %d\n",ring_id);
		flow.ring_id = ring_id;
    if (HashColision[ring_id][flow.hash] == 0) {
      HashColision[ring_id][flow.hash]=1;
      // add the fd to the ring, if needed create a ring, update num of rings, and num of flows within this ring.
		  AddFlow(flow, pRings, uniqueRings);
    }
    else {
      hash_colision_cnt++;
      printf("Hash socket colision found , socket%s:%d - dropped, total %d\n",ip.c_str(),port,hash_colision_cnt);
    }
		if (socketRead == sock_num) {
			printf("read %d sockets from the file\n", socketRead);
			break;
		}
	}
	if (socketRead < sock_num) {
		printf("found only %d socket described in the file\n", socketRead);
	}
	// if (( ringPerFd == false ) && (uniqueRings != socketRead ))
	//{
	//  printf("either allocate rings to all sockets, or to None\n");
	//  exit(-1);
	//}
	//TODO we can have this per ring, no need to limit this
	ringPerFd = (uniqueRings == sock_num);
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
	printf("running checker with:\n\tfds: %d\n \tthreads:%d\n\trings: %d  "
		"\n\tscenario: %s \n\tmin packet %d\n\tmax packet %d "
		"\n\tsleep_time %d\n",
		socketRead, threads_num, uniqueRings, get_sceanrio_str(scenario),
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
  if ((false == ringPerFd) && (threads_num > 1))  {
    printf("multiple threads is not supported yet with commonRings\n");
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
	int prof = 0;
	if (scenario == 2) {
		// hack to start VMA
		int dummy = vma_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		vma_close(dummy);
		struct vma_api_t *vma_api = vma_get_api();
		if (vma_api == NULL) {
			printf("VMA Extra API not found - working with default socket APIs, exiting");
			exit(1);
		}
		vma_ring_type_attr ring;
		ring.ring_type = VMA_RING_CYCLIC_BUFFER;
		// buffer size is 2^17 = 128MB
    	ring.ring_cyclicb.num = (1<<17);
		// user packet size ( not including the un-scattered data
    	ring.ring_cyclicb.stride_bytes = 1400;
		//ring.ring_cyclicb.comp_mask = VMA_RING_TYPE_MASK;
		int res = vma_api->vma_add_ring_profile(&ring, &prof);
		if (res) {
			printf("failed adding ring profile");
			exit(-1);
		}
	}
	// for every ring, open sockets
	for (int i=0; i< MAX_RINGS; i++) {
		if (pRings[i] == NULL ) {
			continue;
		}
		std::vector<sockaddr_in*>::iterator it;
		for (it = pRings[i]->addr_vect.begin();
				it!=pRings[i]->addr_vect.end(); ++it) {
			struct ip_mreqn mc;
			//printf("Adding socket to ring %d\n",i);
			RXSock* pSock = new RXSock;
			pSock->fd = OpenRxSocket(pRings[i]->ring_id,*it,0,argv[1],&mc,prof,!ringPerFd);
			if (pSock->fd <= 0) {
				printf("Error - rx open failed. %d\n", i);
				exit(-1);
			}
			memcpy(&pSock->mc, &mc, sizeof(mc));
			pSock->LastSequenceNumber = -1;
			pSock->lastBlockId = -1;
			pSock->rxCount = 0;
			pSock->rxDrop = 0;
			pSock->statTime = time_get_usec() + 1000*i;
			pSock->index = i;
			pSock->fvalidatePacket = checkpacket;
			pSock->fprintinfo = printdummyInfo;
			pSock->bad_packets = 0;
			pSock->sin_port = ntohs((*it)->sin_port);
			unsigned short hash = hashIpPort2(**it);
			//printf("hash value is %d\n",hash);
			if ( NULL != pRings[i]->hashedSock[hash] ) {
				printf ("Collision, reshuffle your ip addresses \n");
				exit(67);
			}
			pRings[i]->hashedSock[hash] = pSock;
			inet_ntop(AF_INET, &((*it)->sin_addr), pSock->ipAddress, INET_ADDRSTRLEN);
			for (int pid = 0; pid < MAX_PIDS_TS ; pid++) {
				pSock->rPids[pid] = 0x1FFF;
			}
			for (int j = 0; j < 8192; j++) {
				pSock->pidTable[j].lastcc = -1;
				pSock->pidTable[j].rxDrop = 0;
				pSock->pidTable[j].rxCount =0;
			}
			pRings[i]->sock_vect.push_back(pSock);
		}
		if (pRings[i]->numOfSockets == 1) {
			pRings[i]->fvalidatePackets = CheckSingleSocketPackets;
		} else {
			pRings[i]->fvalidatePackets = CheckMultiSocketsPackets;
		}
	}
	// Distribute fds to threads
	for (int var = 0; var < threads_num; ++var) {
		rxThreads[var].sock_len = 0;
	}
	for (int var = 0, ringIdx = 1; var < uniqueRings; ++var, ringIdx++) {
		if (ringIdx >= MAX_RINGS)
			break;
		if (pRings[ringIdx] == NULL) {
			continue;
		}
		int thread_id = var % threads_num;
		printf("Assigning ring %d to thread %d \n", ringIdx, thread_id);
		rxThreads[thread_id].rings.push_back(pRings[ringIdx]);
		rxThreads[thread_id].numOfRings++;
		rxThreads[thread_id].max_s = max_s;
		rxThreads[thread_id].min_s = min_s;
		//ringIdx++;
	}

	for (int i = 0; i < threads_num; i++) {
		switch (scenario) {
		case 0:
			fp_process =run_copy;
			break;
		case 1:
			fp_process =run_zero;
			break;
		case 2:
			fp_process =run_stride;
			break;
		default:
			printf("bad scenario valid is 0-2 got %d\n", scenario);
      		destroyFlows(pRings);
			exit(-1);
			}
		if (pthread_create(&rxThreads[i].t, NULL, fp_process, &rxThreads[i])) {
				fprintf(stderr, "error creating thread\n");
				destroyFlows(pRings);
        		return 1;		
			}
	}
	
	for (int i = 0; i < threads_num; i++) {
		if (pthread_join(rxThreads[i].t, NULL)) {
			fprintf(stderr, "Error creating thread\n");
      		destroyFlows(pRings);
			return 1;
		}
	}
  destroyFlows(pRings);
	exit(0);
}

// multy packet functions, get the entire packet
static void CheckSingleSocketPackets(uint8_t* data, size_t packets, CommonCyclicRing* pRing)
{
//	printf("%s\n",__func__);
	RXSock* pSock = pRing->sock_vect[0];
	for (size_t k = 0; k < packets; k++) {
		pRing->sock_vect[0]->fvalidatePacket(data +42, pRing->sock_vect[0]);

		data += STRIDE_SIZE;
	}
	unsigned long long currentTime = time_get_usec();
	if (currentTime > pSock->statTime) {
		//printf("check cc errors\n");
		pRing->sock_vect[0]->fprintinfo(pSock);
		pSock->statTime = currentTime + PRINT_PERIOD;
	}	
}

static void CheckMultiSocketsPackets(uint8_t* data, size_t packets, CommonCyclicRing* pRing)
{	
//	printf("%s, ring id = %d\n",__func__,pRing->ring_id);
	for (size_t k = 0; k < packets; k++) {		
		unsigned short hash = getHashValFromPacket(data);
		pRing->hashedSock[hash]->fvalidatePacket(data+42,pRing->hashedSock[hash]);
		data+= STRIDE_SIZE;
		}
	unsigned long long currentTime = time_get_usec();
	RXSock* pSock = pRing->sock_vect[0];
		if (currentTime > pSock->statTime) {  
			pRing->PrintInfo();
			pSock->statTime = currentTime + PRINT_PERIOD;
			}
}	
/*
void checkMpegTsPackets(uint8_t* data, size_t packets, CommonCyclicRing* pRing)
{
	RXSock* pSock = pRing->sock_vect[0];
	data += 42;
	for (size_t k = 0; k < packets; k++) {
		checkMpegTsPacket(data, pSock);
		data += STRIDE_SIZE;
	}
	printMpegTsInfo(pSock);
}

void checkRtpPackets(uint8_t* data, size_t packets, CommonCyclicRing* pRing)
{
	RXSock* pSock = pRing->sock_vect[0];
	data += 42;
	for (size_t k = 0; k < packets; k++) {
		checkRtpPacket(data, pSock);
		data += STRIDE_SIZE;
	}
	printRtpInfo(pSock);
}

void checkGVSPV2packets(uint8_t* data, size_t packets, CommonCyclicRing* pRing)
{
	RXSock* pSock = pRing->sock_vect[0];
	data += 42;
	for (size_t k = 0; k < packets; k++) {
		checkGVSPV2packet(data,  pSock);
		data += STRIDE_SIZE;
	}
	printGvspInfo(pSock);
}
*/
void checkpacket(uint8_t* data, RXSock* sock)
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
//		sock->fvalidatePackets = checkMpegTsPackets;
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
	//	sock->fvalidatePackets = checkRtpPackets;
		sock->fprintinfo = printRtpInfo;
		return;
	}
	if ((pdata[0]== 0 ) && ( pdata[1] ==0 )) {
		sock->fvalidatePacket = checkGVSPV2packet;
	//	sock->fvalidatePackets = checkGVSPV2packets;
		sock->fprintinfo = printGvspInfo;
		printf("Socket address %s:%u, will be parsed ad GVSP (default) format\n", sock->ipAddress,sock->sin_port);
	} else {
		printf("failed to parse packet, retry\n");
	}
}
static inline void checkRtpPacket(uint8_t* data, RXSock* sock)
{
	// version == 2 and payload type (PT) is  98 – High bit rate media transport / 27-MHz Clock
	if (((data[0] & 0xC0) == 0x80) && ((data[1] & 0x7f) == sock->rtpPayloadType)) {
		sock->rxCount++;
		uint32_t SequenceNumber = htons(*(uint16_t *) (((uint8_t*) data) + 2));
		uint32_t LostCount = (SequenceNumber + 0x10000
				- sock->LastSequenceNumber - 1) & 0xFFFF;
		if (sock->LastSequenceNumber >= 0)
			sock->rxDrop += LostCount;
		sock->LastSequenceNumber = SequenceNumber;
	} else {
		sock->bad_packets++;
	}

}

// packet base chekers, get the packet data pointer, after the IP/UDP
static inline void checkMpegTsPacket(uint8_t* data, RXSock* sock)
{
	uint16_t pid;
	// skip mac IP and UDP hdr
    //	printf("%s data = 0x%lx\n", __func__, (unsigned long) data);
	//g_totalPacketsProcessed++;
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
							//printf("\n<%s:%u>:(pes-%d): Adding new pid to DB, in index %d, pid 0x%x\n",
								//sock->ipAddress,
								//sock->sin_port,
								//pes, p, pid);
							break;
						}
					}
				}
			}
		} else {
			//if (pes != 0) {
				//printf("error TS packet pes = %d\n", pes);
			
			sock->bad_packets++;
			//uint8_t* temp = data - 42;
			//reportErrorPacket(temp, sock);
		}
	}
}


static void printdummyInfo(RXSock* sock)
{

}

static inline void printRtpInfo(RXSock* sock)
{
	unsigned long long currentTime = time_get_usec();
	if (currentTime < sock->statTime)
		return;

	printf("<%s:%u>: received %d packets, %d drops bad %d\n",
			sock->ipAddress,
			sock->sin_port,
			(int)sock->rxCount/PRINT_PERIOD_SEC,
			(int)sock->rxDrop, sock->bad_packets);
	sock->rxCount = 0;
	sock->rxDrop = 0;
	sock->bad_packets = 0;
	sock->statTime = currentTime + PRINT_PERIOD;
}

static inline void printMpegTsInfo(RXSock* sock)
{
	unsigned long long currentTime = time_get_usec();
	if (currentTime < sock->statTime)
		return;
	sock->statTime = currentTime + PRINT_PERIOD;
	if (sock->rxDrop > 0) {
		printf("<%s:%u>: <PID>: <recieved> <CC ERRORS>\n",
			sock->ipAddress, sock->sin_port);
		for (int p = 0; p < MAX_PIDS_TS; p++) {
			int pid = sock->rPids[p];
		    if ((pid != 0x1FFF) && (sock->pidTable[pid].rxDrop != 0)) {
				printf("\t0x%x:%lu %d\n", (int) pid,
					sock->pidTable[pid].rxCount/PRINT_PERIOD_SEC,
					sock->pidTable[pid].rxDrop);
				sock->pidTable[pid].rxDrop = 0;
				sock->pidTable[pid].rxCount = 0;
			}
		}
		sock->rxDrop = 0;
	} else{
		printf("<%s:%u>: Recieived %lu packets, No cc Errors\n",
				sock->ipAddress,sock->sin_port,sock->rxCount);
		sock->rxCount=0;
	}
}

#define GVSP_PT_INIT -1
#define GVSP_PT_TRAILER 2
#define GVSP_PT_LEADER  1
#define GVSP_PT_PAYLOAD 3

static inline void printGvspInfo(RXSock* sock)
{
	unsigned long long currentTime = time_get_usec();
	if (currentTime < sock->statTime)
		return;
	sock->statTime = currentTime + PRINT_PERIOD;
	printf("GVSP<%s:%u>: received %d packets, %d drops bad %d\n",
			sock->ipAddress,sock->sin_port,
			(int)sock->rxCount/PRINT_PERIOD_SEC,
			(int)sock->rxDrop,
			sock->bad_packets);
	sock->rxCount = 0;
	sock->rxDrop = 0;
	sock->bad_packets = 0;
}

void checkGVSPV2packet(uint8_t* data, RXSock* sock)
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


unsigned short hashIpPort2(sockaddr_in addr )
{
  int hash = ((size_t)(addr.sin_addr.s_addr) * 59) ^ ((size_t)(addr.sin_port) << 16);
  unsigned char smallHash = (unsigned char)(((unsigned char) ((hash*19) >> 24 ) )  ^ ((unsigned char) ((hash*17) >> 16 )) ^ ((unsigned char) ((hash*5) >> 8) ) ^ ((unsigned char) hash));
  unsigned short mhash = (((addr.sin_addr.s_addr & 0xd) << 7) | smallHash ) ;
  return mhash;
}
#define IP_HEADER_OFFSET 14
#define IP_HEADER_SIZE   20
#define IP_DEST_OFFSET   (IP_HEADER_OFFSET+ 16)
#define UDP_HEADER_OFFSET (IP_HEADER_SIZE + IP_HEADER_OFFSET )
#define PORT_DEST_OFFSET  (UDP_HEADER_OFFSET + 2)

unsigned short getHashValFromPacket(uint8_t* data)
{
	unsigned int* pIP = (unsigned int*)&data[IP_DEST_OFFSET];
	unsigned short* pPort = (unsigned short*)&data[PORT_DEST_OFFSET];
	int hash = ((size_t)(*pIP) * 59) ^ ((size_t)(*pPort) << 16);
  unsigned char smallHash = (unsigned char)(((unsigned char) ((hash*19) >> 24 ) )  ^ ((unsigned char) ((hash*17) >> 16 )) ^ ((unsigned char) ((hash*5) >> 8) ) ^ ((unsigned char) hash));
  unsigned short mhash = (((*pIP & 0xd) << 7) | smallHash ) ;
  printf("0x%x\n",(*pIP));
	//printf(" IP address is %u, port is %u, hash val is %u\n",(unsigned) data[IP_DEST_OFFSET],(unsigned )data[PORT_DEST_OFFSET],smallHash);
	return mhash;	
}


