/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2016 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <setjmp.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <math.h>

#include <rte_common.h>
#include <rte_log.h>
#include <rte_malloc.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_pci.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_ring.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>

#include <sys/time.h>
#include <arpa/inet.h>

/**
 * @author: tsf
 * @created: 2019-06-05
 * @modified: 2020-07-15
 * @desc: convert this app to external DPDK-16.07-based int-collector.
 *        and support to parse ML-DATA.
 */

/* do not shown dpdk configuration initialization. */
#define CONFIG_NOT_DISPLAY

#define ETH_HEADER_LEN              14
#define IPV4_HEADER_LEN             20
#define IPV4_SRC_BASE               26
#define IPV4_SRC_LEN                 4
#define IPV4_DST_BASE               30
#define IPV4_DST_LEN                 4
#define IPV4_IP_LEN                  8    // <src, dst>

#define INT_HEADER_BASE             34
#define INT_HEADER_LEN               5
#define INT_HEADER_TYPE_OFF         34
#define INT_HEADER_TYPE_LEN          2
#define INT_HEADER_TTL_OFF          36
#define INT_HEADER_TTL_LEN           1
#define INT_HEADER_MAPINFO_OFF      37
#define INT_HEADER_MAPINFO_LEN       2
#define INT_HEADER_DATA_OFF         39

/* tsf: INT data len. */
#define INT_DATA_DPID_LEN            4
#define INT_DATA_IN_PORT_LEN         4
#define INT_DATA_OUT_PORT_LEN        4
#define INT_DATA_INGRESS_TIME_LEN    8
#define INT_DATA_HOP_LATENCY_LEN     4
#define INT_DATA_BANDWIDTH_LEN       4
#define INT_DATA_N_PACKETS_LEN       8
#define INT_DATA_N_BYTES_LEN         8
#define INT_DATA_QUEUE_LEN           4
#define INT_DATA_FWD_ACTS_LEN        4
#define INT_DATA_BER_LEN             8

#define INT_TYPE_VAL             0x0908

/* host-byte order <-> network-byte order. */
#define htonll(_x)    ((1==htonl(1)) ? (_x) : \
                           ((uint64_t) htonl(_x) << 32) | htonl(_x >> 32))
#define ntohll(_x)    ((1==ntohl(1)) ? (_x) : \
                           ((uint64_t) ntohl(_x) << 32) | ntohl(_x >> 32))

#define Max(a, b) ((a) >= (b) ? (a) : (b))
#define Min(a, b) ((a) <= (b) ? (a) : (b))
#define AbsMinus(a, b) abs(a-b)

/* Unsigned.  */
# define UINT8_C(c)	c
# define UINT16_C(c)	c
# define UINT32_C(c)	c ## U
# if __WORDSIZE == 64
#  define UINT64_C(c)	c ## UL
# else
#  define UINT64_C(c)	c ## ULL
# endif

/* tsf: #define to control code branch */
//#define L2_FWD_APP  // if want to run original l2fwd app, uncomment '#define L2_FWD'
//#define PRINT_NODE_RESULT   // if want to print nodes' parsed INT metadata result, uncomment '#define PRINT_NODE_RESULT'
//#define PRINT_LINK_RESULT   // if want to print link's information, uncomment '#define PRINT_LINK_RESULT'
#define INT_TYPE_CHECK      // if want to check INT type (0x0908), uncomment '#define INT_TYPE_CHECK'

#define TIME_INTERVAL_SHOULD_WRITE // if want to set time interval to print result, uncomment '#define TIME_INTERVAL_SHOULD_WRITE'
//#define PKT_INTERVAL_SHOULD_WRITE  // if want to set packet interval to print result, uncomment '#define PKT_INTERVAL_SHOULD_WRITE'
#define PRINT_SECOND_PERFORMANCE   // if want to print collector's performance, uncomment '#define PRINT_SECOND_PERFORMANCE'

/* tsf: this is real running environment, and only one is in uncomment state. */
#define SOCK_DA_TO_OCM      // if want to send 'ber' to OCM controller, uncomment '#define SOCK_DA_TO_OCM'
//#define SOCK_DA_TO_DL      // if want to send 'bd_to_dl_info_t' to DL module, uncomment '#define SOCK_DA_TO_DL'

/* tsf: this is test running environment, and only one is in uncomment state. */
//#define TEST_READ_TRACE_FROM_TXT_SEND_TO_DL    // if want to read trace form txt then send to DL module, uncomment '#define TEST_READ_TRACE_FROM_TXT_SEND_TO_DL'
//#define TEST_BER_SEND_TO_OCM_AGENT             // if want to send 'ber' to OCM agent to adjust ocm collection policy, uncomment '#define TEST_BER_SEND_TO_OCM_AGENT'

/* write data every time interval. */
#define ONE_SECOND_IN_US           1000000.0   // us
#define TIME_WRITE_THRESH          50000.0     // us, can be adjusted

/* tsf: definition for struct 'bd_to_dl_info_t' */
#define DL_COLLECTED_NODES 3      // how many bandwidth that we need to send DL module
#define OCM_COLLECTOR_COLLECTED_NODES 3  // how many bandwidth that we need to send ocm collector module

#define BW_CAL_PERIOD              50000.0     // in ovs-pof, calculate bandwidth every 50 ms
#define BW_WIN_WIDTH_IN_SECOND     50          // theoretically: ((int) ceil(ONE_SECOND_IN_US / BW_CAL_PERIOD)) = 20,
                                               // how many different values we can collect in one second, we make it bigger
#define LINK_BW_CAPACITY           10000.0     // Mbps, = 10 Gbps
#define ROUND_PRECISION            4           // decimal precision

/* write data every packet interval. */
//#define PKT_WRITE_THRESH           1000       // pkts, can be adjusted

/* supported mapInfo */
#define CPU_BASED_MAPINFO           0x06ff
#define NP_BASED_MAPINFO            0x031f

/* device number on the link. */
#define MAX_DEVICE        14
#define MIN_PKT_LEN       60

/*
 * packet-level info.
 * INT Header: Metadata set.
 * */
typedef struct {
//    uint16_t type;      /* INT type = 0x0908 */

//    uint8_t  hops;
//    uint16_t map_info;    /* bitmap */

    /* IP layer data. */
    uint32_t switch_id;
    uint32_t in_port;
    uint32_t out_port;
    uint32_t hop_latency;
    uint64_t ingress_time;
    float bandwidth;
    uint64_t n_packets;
    uint64_t n_bytes;
    uint32_t queue_len;
    uint32_t fwd_acts;

    /* optical layer data. */
    double ber;

    uint32_t hash;           /* indicate whether to store into files. */
} int_item_t;

/* store result separately */
FILE *fp_performance;  /* second performance. */
FILE *fp_norm_bd, *fp_int_info;  /* SOCK_DA_TO_DL */
FILE *fp_ber_arr; /* SOCK_DA_TO_OCM */

/*
 * flow-level info. for single flow.
 * */
typedef struct {
    uint32_t ufid;                /* unique flow id. <src, dst>. */
    uint32_t links[MAX_DEVICE];   /* the flow's path. */

    uint8_t  hops;       /* i.e., ttl */
    uint8_t  pad;
    uint16_t map_info;    /* bitmap */

    /* below element should be completed by DB */
//    uint64_t start_time;          /* service start time. minimum ingress_time. */
//    uint64_t end_time;            /* service end time. maximum ingress_time. */

//    int_item_t his_pkt_info[MAX_DEVICE];      /* historical packet-level info. */
    int_item_t cur_pkt_info[MAX_DEVICE];      /* current packet-level info. */
//
    uint32_t jitter_delay[MAX_DEVICE];        /* jitter = cur.latency - his.latency. */
    uint32_t max_delay[MAX_DEVICE];           /* max_delay = max(his.latency). */
//
//    uint16_t drop_reason[MAX_DEVICE];         /* 0: no drop
//                                               * 1: TODO: Deep Learning or other methods judge the drop reason
//                                               */

} flow_info_t;

/*
 * ufid = <src, dst>
 * */
static inline uint32_t get_ufid(uint8_t *pkt_header) {
    uint32_t src_ip, dst_ip;
    uint32_t ufid = 0;

    memcpy(&src_ip, pkt_header + IPV4_SRC_BASE, IPV4_SRC_LEN);
    memcpy(&dst_ip, pkt_header + IPV4_DST_BASE, IPV4_DST_LEN);

    ufid = src_ip ^ (dst_ip >> 8);
    ufid = (ufid/10%10) * 10 + ufid % 10;       // range = [0, 100]

    return ufid;
}

static inline uint64_t get_flow_start_time(uint64_t a, uint64_t b) {
    if (a == 0) {
        a = 0xffffffffffffffff;
    }

    if (b == 0) {
        b = 0xffffffffffffffff;
    }

    return Min(a, b);
}

static inline uint64_t get_flow_end_time(uint64_t a, uint64_t b) {

    return Max(a, b);
}

static void print_pkt(uint32_t pkt_len, uint8_t *pkt){
    uint32_t i = 0;
    for (i = 0; i < pkt_len; ++i) {
        //printf("%02x", i, pkt[i]);

        if ((i + 1) % 8 == 0) {
            printf(" ");   // 2 space
        }

        if ((i + 1) % 16 == 0) {
            printf("\n");
        }
    }
}

static inline unsigned long long rp_get_us(void) {
    struct timeval tv = {0};
    gettimeofday(&tv, NULL);
    return (unsigned long long) (tv.tv_sec * 1000000L + tv.tv_usec);
}

static inline unsigned long long rp_get_ns(void) {
    struct timespec cur;
    clock_gettime(CLOCK_MONOTONIC, &cur);
    return (unsigned long long) (cur.tv_sec * 1e9L + cur.tv_nsec);
}


/* used for performance test per second. */
uint32_t port_recv_int_cnt = 0, sec_cnt = 0, write_cnt = 0;
double start_time = 0, end_time = 0;

/* used for relative timestamp. */
double relative_time = 0, delta_time = 0;        // write a record with a relative timestamp
double relative_start_time = 0;                  // when first pkt comes in, timer runs
bool first_pkt_in = true;                        // when first pkt comes in, turn 'false'

/* used for INT item. */
#define MAX_FLOWS 100
flow_info_t flow_infos[MAX_FLOWS] = {0};

static volatile bool force_quit;

#define RTE_LOGTYPE_L2FWD RTE_LOGTYPE_USER1
#define RTE_LOGTYPE_OCMSOCK RTE_LOGTYPE_USER2

#define NB_MBUF   8192

#define MAX_PKT_BURST 32
#define BURST_TX_DRAIN_US 100 /* TX drain every ~100us */
#define MEMPOOL_CACHE_SIZE 256

/*
 * Configurable number of RX/TX ring descriptors
 */
#define RTE_TEST_RX_DESC_DEFAULT 128
#define RTE_TEST_TX_DESC_DEFAULT 512
static uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
static uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;

/* ethernet addresses of ports */
static struct ether_addr l2fwd_ports_eth_addr[RTE_MAX_ETHPORTS];

/* mask of enabled ports */
static uint32_t l2fwd_enabled_port_mask = 0;

/* list of enabled ports */
static uint32_t l2fwd_dst_ports[RTE_MAX_ETHPORTS];

static unsigned int l2fwd_rx_queue_per_lcore = 1;

#define MAX_RX_QUEUE_PER_LCORE 16
#define MAX_TX_QUEUE_PER_PORT 16
struct lcore_queue_conf {
	unsigned n_rx_port;
	unsigned rx_port_list[MAX_RX_QUEUE_PER_LCORE];
} __rte_cache_aligned;
struct lcore_queue_conf lcore_queue_conf[RTE_MAX_LCORE];

static struct rte_eth_dev_tx_buffer *tx_buffer[RTE_MAX_ETHPORTS];

static const struct rte_eth_conf port_conf = {
	.rxmode = {
		.split_hdr_size = 0,
		.header_split   = 0, /**< Header Split disabled */
		.hw_ip_checksum = 0, /**< IP checksum offload disabled */
		.hw_vlan_filter = 0, /**< VLAN filtering disabled */
		.jumbo_frame    = 0, /**< Jumbo Frame Support disabled */
		.hw_strip_crc   = 0, /**< CRC stripped by hardware */
	},
	.txmode = {
		.mq_mode = ETH_MQ_TX_NONE,
	},
};

struct rte_mempool * l2fwd_pktmbuf_pool = NULL;

/* Per-port statistics struct */
struct l2fwd_port_statistics {
	uint64_t tx;
	uint64_t rx;
	uint64_t dropped;
} __rte_cache_aligned;
struct l2fwd_port_statistics port_statistics[RTE_MAX_ETHPORTS];

#define MAX_TIMER_PERIOD 86400 /* 1 day max */
/* A tsc-based timer responsible for triggering statistics printout */
static uint64_t timer_period = 10; /* default period is 10 seconds */

/* Running x second, then automatically quit. used in process_int_pkt() */
static uint32_t timer_interval = 0;   /* default processing time. 0 means always running. */

static bool SOCK_SHOULD_BE_RUN = false;    /* default false. */

/* Print out statistics on packets dropped */
static void
print_stats(void)
{
	uint64_t total_packets_dropped, total_packets_tx, total_packets_rx;
	unsigned portid;

	total_packets_dropped = 0;
	total_packets_tx = 0;
	total_packets_rx = 0;

	const char clr[] = { 27, '[', '2', 'J', '\0' };
	const char topLeft[] = { 27, '[', '1', ';', '1', 'H','\0' };

		/* Clear screen and move to top left */
	printf("%s%s", clr, topLeft);

	printf("\nPort statistics ====================================");

	for (portid = 0; portid < 4; portid++) {  // tsf: limited to 4
		/* skip disabled ports */
		if ((l2fwd_enabled_port_mask & (1 << portid)) == 0)
			continue;
		printf("\nStatistics for port %u ------------------------------"
			   "\nPackets sent: %24"PRIu64
			   "\nPackets received: %20"PRIu64
			   "\nPackets dropped: %21"PRIu64,
			   portid,
			   port_statistics[portid].tx,
			   port_statistics[portid].rx,
			   port_statistics[portid].dropped);

		total_packets_dropped += port_statistics[portid].dropped;
		total_packets_tx += port_statistics[portid].tx;
		total_packets_rx += port_statistics[portid].rx;

//		fprintf(fp, "%u\t %lu\t %lt\t %lu\t \n", portid, port_statistics[portid].tx,
//                port_statistics[portid].rx, port_statistics[portid].dropped);

		// clear, then "-T 1" is pkt/s
        port_statistics[portid].tx = 0;
        port_statistics[portid].rx = 0;
        port_statistics[portid].dropped = 0;
	}
	printf("\nAggregate statistics ==============================="
		   "\nTotal packets sent: %18"PRIu64
		   "\nTotal packets received: %14"PRIu64
		   "\nTotal packets dropped: %15"PRIu64,
		   total_packets_tx,
		   total_packets_rx,
		   total_packets_dropped);
	printf("\n====================================================\n");
}

/* equal to rte_pktmbuf_mtod() */
static inline void *
dp_packet_data(const struct rte_mbuf *m)
{
    return m->data_off != UINT16_MAX
           ? (uint8_t *) m->buf_addr + m->data_off : NULL;
}

static inline uint8_t get_set_bits_of_bytes(uint16_t byte){
    uint8_t count = 0;
    while (byte) {
        count += byte & 1;
        byte >>= 1;
    }
    return count;
}


static uint32_t simple_linear_hash(int_item_t *item) {
    /* hop_latency and ingress_time are volatile, do not hash them. */
    static int prime = 31;
    uint32_t hash = item->switch_id * prime + prime;
    hash += item->in_port * prime;
    hash += item->out_port * prime;

    item->hash = hash;

    return hash;
}

static void signal_handler(int signum);

int i;  /* used for 'ttl', indicate i-th stack field */

/* indicate DB to recognize data type */
enum DATA_OUTPUT_TYPE {
    NODE_INT_INFO = 1,
    LINK_PATH = 2
} data_output_type;

/* switch type */
enum SWITCH_TYPE {
    OVS_POF = 0,
    TOFINO = 1
};

/* tofino has 'bos' at bit 32 every 4B. */
uint32_t bos_bit[2] = {0xffffffff, 0x7fffffff};

flow_info_t flow_info = {0};

#ifdef SOCK_DA_TO_DL   // SOCK_DA_TO_DL
    #define SERVER_ADDR "192.168.108.221"   // DL server
    #define SOCKET_OCM_PORT 2020
#endif
#ifdef SOCK_DA_TO_OCM // SOCK_DA_TO_OCM
    #define SERVER_ADDR "192.168.108.221"  // OCM collector server
    #define SOCKET_OCM_PORT 2018
#endif

#define MAXLINE 1024
#define PENDING_QUEUE 10
#define SLEEP_SECONDS 1

pthread_t tid_sock_process_ocm_thread;   // init ocm socket setup with a thread
bool BER_TCP_SOCK_CLIENT_RUN_ONCE = true;    // only run once, then turn to false

int clientfd_ocm = 0;     // socket
char buf_send_ber[MAXLINE] = {0};
char buf_send_bd[MAXLINE] = {0};
bool send_flag = 0;     // 1, send data; 0, stop sending; default as 0, turn to 1 when sock connects it.
int send_times = 0;     // reset for every sock 'accept'

double cur_ber = 0, his_ber = 0;   // ber info

typedef struct struct_ber_to_da_info {  // data sent to ocm collector module
//    int sec;
    double ber[OCM_COLLECTOR_COLLECTED_NODES];
} ber_to_ocm_collector_info_t;
ber_to_ocm_collector_info_t ber_to_ocm_collector_info = {0};

/* tsf: DL-related bandwidth struct. theoretically, max(bd_win_num) = 20. but, ovs-pof's revalidate threads may calculate
 *      bandwidth less than 50 ms (then bd_win_num > 20) because fast-path flow rules are removed.
 * */
typedef struct struct_win_bd_info {  // to calculate bandwidth at second scale using sliding windows
    float cur_win_bd[DL_COLLECTED_NODES][BW_WIN_WIDTH_IN_SECOND];  // tsf: bd_to_dl_info_t.bandwidth = average(sum(win_bd[BW_WIN_WIDTH_IN_SECOND]))
    int bd_win_num[DL_COLLECTED_NODES]; // tsf: indicate now locate in which window, when bd_win_num == BW_WIN_WIDTH_IN_SECOND, send to DL and reset as 0.

    int sec;                   // tsf: record the time
    int global_time_win_num;   // tsf: indicate which one of 50 ms in a second, reset every second.
    bool sec_should_write;     // tsf: indicate one second is satisfied, and bandwidth should be calculated to get average
} bd_win_info_t;
bd_win_info_t bd_win_info = {0};

typedef struct struct_bd_to_dl_info {  // data sent to DL module
    int sec; // cur_sec time
    float bandwidth[DL_COLLECTED_NODES];
} bd_to_dl_info_t;
bd_to_dl_info_t bd_to_dl_info = {0};


/* tsf: bandwidth average function. after collecting BW_WIN_WIDTH_IN_SECOND points every 50ms, then call this function
 * to get average bandwidth at second scale.
 * */
static void get_mean_bandwidth_at_second_scale(bd_win_info_t *bd_win_info, bd_to_dl_info_t *bd_to_dl_info) {

    int validate_wins;  // each monitored node has one unique validated window number recorder
    float decimal_round_factor = pow(10, ROUND_PRECISION);

    /* record time to align data. */
    bd_to_dl_info->sec = bd_win_info->sec;

    /* get average normalized bandwidth. */
    int cur_switch, cur_win;
    for (cur_switch = 0; cur_switch < DL_COLLECTED_NODES; cur_switch++) {

        validate_wins = 0;
        for (cur_win = 0; cur_win <= bd_win_info->bd_win_num[cur_switch]; cur_win++) {  // win_num = [0, bd_win_num[cur_switch]]
            /* tsf: NOTE! two cases when bd < 1.0:
             * 1. when ovs-pof starts, its bandwidth period may less than 50 ms, thus bd < 1.0
             * 2. when in init stage of bd_win_info, its bandwidth is assgined as 0, thus ba < 1.0
             * */
            if (bd_win_info->cur_win_bd[cur_switch][cur_win] >= 1.0) {
                bd_to_dl_info->bandwidth[cur_switch] += bd_win_info->cur_win_bd[cur_switch][cur_win];       // sum
                validate_wins += 1;
            }
        }

        /*printf("get_mean_bandwidth_at_second_scale, cur_switch: %d, cur_wins: %d, validate_wins: %d\n", cur_switch, cur_win, validate_wins);*/

        if (validate_wins > 0) { // avoid nan when validate_wins = 0
            bd_to_dl_info->bandwidth[cur_switch] = bd_to_dl_info->bandwidth[cur_switch] / validate_wins;    // mean
            bd_to_dl_info->bandwidth[cur_switch] = bd_to_dl_info->bandwidth[cur_switch] / LINK_BW_CAPACITY; // normalization in [0, 1]
            bd_to_dl_info->bandwidth[cur_switch] = round(bd_to_dl_info->bandwidth[cur_switch] * decimal_round_factor) /
                                                   decimal_round_factor; // keep decimal precision
        }
    }

//    printf("get_mean_bandwidth_at_second_scale: %d sec, %f %f %f\n", bd_win_info->sec, bd_to_dl_info->bandwidth[0],
//            bd_to_dl_info->bandwidth[1], bd_to_dl_info->bandwidth[2]);
    fprintf(fp_norm_bd, "%d\t %f\t %f\t %f\n", bd_win_info->sec, bd_to_dl_info->bandwidth[0],
           bd_to_dl_info->bandwidth[1], bd_to_dl_info->bandwidth[2]);
}

/* tsf: parse, filter and collect the INT fields. */
static void process_int_pkt(struct rte_mbuf *m, unsigned portid) {
    uint8_t *pkt = dp_packet_data(m);   // packet header
    uint32_t pkt_len = m->pkt_len;      // packet len

    /* Packet length check. */
    if (pkt_len < MIN_PKT_LEN) {
        return;
    }

    uint32_t ufid = get_ufid(pkt);
    /*printf("ufid: 0x%04x\n", ufid);*/

    /* used to indicate where to start to parse. */
    uint32_t pos = INT_HEADER_BASE;

    /*
     * ===================== REJECT STAGE =======================
     * */
    /* INT type check. */
#ifdef INT_TYPE_CHECK  // if want to check INT type (0x0908), uncomment '#define INT_TYPE_CHECK'
    uint16_t type = (pkt[pos++] << 8) + pkt[pos++];
    uint8_t ttl = pkt[pos++];

    /*printf("type: 0x%04x, ttl: %x\n", type, ttl);*/

    if (type != INT_TYPE_VAL || ttl == 0x00) {
        return;
    }
#endif

    /* MapInfo check. */
    uint16_t map_info = (pkt[pos++] << 8) + pkt[pos++];
    /*printf("mapInfo: 0x%04x, bitmaps: %d\n", map_info, get_set_bits_of_bytes(map_info));*/
    if (get_set_bits_of_bytes(map_info) == 0) {
        return;
    }

    /* first_int_pkt_in, init the 'relative_start_time' */
    if (first_pkt_in) {
        relative_start_time = rp_get_us();
        start_time = relative_start_time;

        /* init global variables once. */
        memset(&flow_info, 0x00, sizeof(flow_info_t));

#ifdef SOCK_DA_TO_DL
        memset(&bd_win_info, 0x00, sizeof(bd_win_info_t));
        memset(&bd_win_info, 0x00, sizeof(bd_to_dl_info_t));
#endif

#ifdef SOCK_DA_TO_OCM
        memset(&ber_to_ocm_collector_info, 0x00, sizeof(ber_to_ocm_collector_info_t));
#endif

        first_pkt_in = false;
    }

    flow_info.ufid = ufid;
    flow_info.hops = ttl;
    flow_info.map_info = map_info;

    /* port processing packet rate in 1s, clear after per sec. */
    port_recv_int_cnt++;

    bool time_interval_should_write = false;
#ifdef TIME_INTERVAL_SHOULD_WRITE  // if want to set time interval to print result, uncomment '#define TIME_INTERVAL_SHOULD_WRITE'
    end_time = rp_get_us();
    relative_time = (end_time - relative_start_time) / ONE_SECOND_IN_US;
    delta_time = end_time - start_time;

    if (delta_time >= TIME_WRITE_THRESH) { // us, threshold 'TIME_WRITE_THRESH' can be adjusted
        time_interval_should_write = true;
        start_time = end_time;

#ifdef SOCK_DA_TO_DL
        bd_win_info.global_time_win_num += 1;  // to show how many times of 'should_write', reset every second
#endif
    }
#endif

    bool pkt_interval_should_write = false;
#ifdef PKT_INTERVAL_SHOULD_WRITE   // if want to set packet interval to print result, uncomment '#define PKT_INTERVAL_SHOULD_WRITE'
    if (port_recv_int_cnt % PKT_WRITE_THRESH == 0) {
        pkt_interval_should_write = true;
    }
#endif

    /*
     * ===================== PARSE STAGE =======================
     * */
    uint16_t switch_map_info = map_info;
    uint32_t switch_id = 0;
    uint32_t in_port = 0;
    uint32_t out_port = 0;
    uint64_t ingress_time = 0;
    uint32_t hop_latency = 0;
    float bandwidth = 0;
    uint64_t n_packets = 0;
    uint64_t n_bytes = 0;
    uint32_t queue_len = 0;
    uint32_t fwd_acts = 0;
    double ber;

    uint32_t switch_type = 0;
    for (i = 0; i < ttl; i++) {
        if (map_info & 0x1) {
            switch_id = (pkt[pos++] << 24) + (pkt[pos++] << 16) + (pkt[pos++] << 8) + pkt[pos++];
        } else {
            switch_id = 0;  // unlikely
        }
        flow_info.cur_pkt_info[i].switch_id = switch_id;


        flow_info.links[i] = switch_id;
        /*printf("switch_id: 0x%08x\n", switch_id);*/

        /* distinguish switch. */
        if ((0xff000000 & switch_id) == 0x00) {   // device: ovs-pof
            switch_map_info = map_info & CPU_BASED_MAPINFO;
            switch_type = OVS_POF;
            /*printf("ovs-final_mapInfo: 0x%04x\n", switch_map_info);*/
        } else {
            switch_map_info = map_info & NP_BASED_MAPINFO;
            switch_type = TOFINO;
            /*printf("tofino-final_mapInfo: 0x%04x\n", switch_map_info);*/
        }
        switch_id = switch_id & bos_bit[switch_type];

        if (switch_map_info & (UINT16_C(1) << 1)) {
            in_port = (pkt[pos++] << 24) + (pkt[pos++] << 16)
                      + (pkt[pos++] << 8) + pkt[pos++];
        } else {
            in_port = 0;
        }
        flow_info.cur_pkt_info[i].in_port = in_port & bos_bit[switch_type];
        /*printf("ufid:%x, pkt_i:%d, in_port: 0x%08x\n", ufid, i, in_port);*/

        if (switch_map_info & (UINT16_C(1)  << 2)) {
            out_port = (pkt[pos++] << 24) + (pkt[pos++] << 16)
                       + (pkt[pos++] << 8) + pkt[pos++];
        } else {
            out_port = 0;
        }
        flow_info.cur_pkt_info[i].out_port = out_port & bos_bit[switch_type];
        /*printf("ufid:%x, pkt_i:%d, out_port: 0x%08x\n", ufid, i, out_port);*/

        if (switch_map_info & (UINT16_C(1)  << 3)) {
            memcpy(&ingress_time, &pkt[pos], INT_DATA_INGRESS_TIME_LEN);
            ingress_time = ntohll(ingress_time);
            pos += INT_DATA_INGRESS_TIME_LEN;
        } else {
            ingress_time = 0;
        }
        if (switch_type == TOFINO) {
            ingress_time = ingress_time & bos_bit[TOFINO];
        }
        flow_info.cur_pkt_info[i].ingress_time = ingress_time;
        /*printf("ufid:%x, pkt_i:%d, ingress_time: 0x%016lx\n", ufid, i, ingress_time);*/

        if (switch_map_info & (UINT16_C(1)  << 4)) {
            hop_latency = (pkt[pos++] << 24) + (pkt[pos++] << 16)
                          + (pkt[pos++] << 8) + pkt[pos++];
        } else {
            hop_latency = 0;
        }

        /* max_delay and jitter delay. */
        flow_info.max_delay[i] = Max(hop_latency, flow_info.max_delay[i]);
        uint32_t his_hop_latency = flow_info.cur_pkt_info[i].hop_latency;
        flow_info.jitter_delay[i] = AbsMinus(hop_latency, his_hop_latency);
        flow_info.cur_pkt_info[i].hop_latency = hop_latency & bos_bit[switch_type];
        /*printf("ufid:%x, pkt_i:%d, hop_latency: 0x%08x\n", ufid, i, hop_latency);*/
        /*printf("ufid:%x, pkt_i:%d, latency:%d, jitter: %d, max_delay:%d\n",
                ufid, i, hop_latency, flow_info.jitter_delay[i], flow_info.max_delay[i]);*/

        if (switch_map_info & (UINT16_C(1)  << 5)) {
            memcpy(&bandwidth, &pkt[pos], INT_DATA_BANDWIDTH_LEN);
            pos += INT_DATA_BANDWIDTH_LEN;
        } else {
            bandwidth = 0;
        }
        flow_info.cur_pkt_info[i].bandwidth = bandwidth;
        /*printf("ufid:%x, pkt_i:%d, bandwidth: %f\n", ufid, i, bandwidth);*/

        if (switch_map_info & (UINT16_C(1)  << 6)) {
            memcpy(&n_packets, &pkt[pos], INT_DATA_N_PACKETS_LEN);
            n_packets = ntohll(n_packets);
            pos += INT_DATA_N_PACKETS_LEN;
        } else {
            n_packets = 0;
        }
        if (switch_type == TOFINO) {
            n_packets = 0;    // tofino not supported
        }
        flow_info.cur_pkt_info[i].n_packets = n_packets;
        /*printf("ufid:%x, pkt_i:%d, n_packets: 0x%016lx\n", ufid, i, n_packets);*/

        if (switch_map_info & (UINT16_C(1)  << 7)) {
            memcpy(&n_bytes, &pkt[pos], INT_DATA_N_BYTES_LEN);
            n_bytes = ntohll(n_bytes);
            pos += INT_DATA_N_BYTES_LEN;
        } else {
            n_bytes = 0;
        }
        if (switch_type == TOFINO) {
            n_bytes = 0;    // tofino not supported
        }
        flow_info.cur_pkt_info[i].n_bytes = n_bytes;
        /*printf("ufid:%x, pkt_i:%d, n_bytes: 0x%016lx\n", ufid, i, n_bytes);*/


        if (switch_map_info & (UINT16_C(1)  << 8)) {
            queue_len = (pkt[pos++] << 24) + (pkt[pos++] << 16)
                        + (pkt[pos++] << 8) + pkt[pos++];
        } else {
            queue_len = 0;
        }
        flow_info.cur_pkt_info[i].queue_len = queue_len & bos_bit[switch_type];
       /*printf("ufid:%x, pkt_i:%d, queue_len: %d\n", ufid, i, queue_len);*/

        if (switch_map_info & (UINT16_C(1)  << 9)) {
            fwd_acts = (pkt[pos++] << 24) + (pkt[pos++] << 16)
                       + (pkt[pos++] << 8) + pkt[pos++];
        } else {
            fwd_acts = 0;
        }
        flow_info.cur_pkt_info[i].fwd_acts = fwd_acts & bos_bit[switch_type];
        /*printf("ufid:%x, pkt_i:%d, fwd_acts: 0x%08x\n", ufid, i, fwd_acts);*/

        if (switch_map_info & (UINT16_C(1)  << 10)) {
            memcpy(&ber, &pkt[pos], INT_DATA_BER_LEN);
            pos += INT_DATA_BER_LEN;
        } else {
            ber = 0;
        }
        flow_info.cur_pkt_info[i].ber = ber;
//        printf("parsed_ber: %f\n", ber);
    }

    flow_info.links[i] = '\0';
//    flow_info.start_time = get_flow_start_time(flow_info.his_pkt_info[0].ingress_time,
//            flow_info.cur_pkt_info[0].ingress_time);  // hop 0 of the link
//    flow_info.end_time = get_flow_end_time(flow_info.his_pkt_info[ttl-1].ingress_time,
//            flow_info.cur_pkt_info[ttl-1].ingress_time);  // hop ttl of the link

    /* output result about flow_info. <cur, his> */
    if (time_interval_should_write || pkt_interval_should_write) { // we can choose two schemes, or any either one.
        // TODO: how to output

        unsigned long long print_timestamp = rp_get_us();
        /* print node's INT info, for each node in links */
        for (i = 0; i < ttl; i++) {
#ifdef PRINT_NODE_RESULT  // if want to print, uncomment '#define PRINT_NODE_RESULT'
            printf("%d\t %d\t %llu\t %d\t %d\t %d\t %ld\t %d\t %f\t %ld\t %ld\t %d\t %d\t %.16g\t\n",
                   NODE_INT_INFO, ufid, print_timestamp,
                   flow_info.cur_pkt_info[i].switch_id, flow_info.cur_pkt_info[i].in_port,
                   flow_info.cur_pkt_info[i].out_port, flow_info.cur_pkt_info[i].ingress_time,
                   flow_info.cur_pkt_info[i].hop_latency, flow_info.cur_pkt_info[i].bandwidth,
                   flow_info.cur_pkt_info[i].n_packets, flow_info.cur_pkt_info[i].n_bytes,
                   flow_info.cur_pkt_info[i].queue_len, flow_info.cur_pkt_info[i].fwd_acts,
                   flow_info.cur_pkt_info[i].ber);
#endif
        }


#ifdef PRINT_LINK_RESULT  // if want to print link's information, uncomment '#PRINT_LINK_RESULT'
        /* print link path */
        printf("%d\t %d\t %llu\t ", LINK_PATH, ufid, print_timestamp);
        for (i = 0; i < ttl; i++) {
            if (flow_info.links[i] == '\0') {
                break;
            }

            printf("%d\t ", flow_info.links[i]);
        }
        printf("\n");
#endif

        write_cnt++;
    }

//    memcpy(flow_info.his_pkt_info, flow_info.cur_pkt_info, sizeof(flow_info.cur_pkt_info));

    /* output how many packets we can parse in a second. */
    if ((end_time - relative_start_time) >= (ONE_SECOND_IN_US * (sec_cnt+1))) {
        sec_cnt++;

#ifdef SOCK_DA_TO_DL
        bd_win_info.sec_should_write = true;  // reset every second
        bd_win_info.sec = sec_cnt;
#endif

#ifdef SOCK_DA_TO_OCM
//        ber_to_ocm_collector_info.sec = sec_cnt;
#endif

#ifdef PRINT_SECOND_PERFORMANCE  // if want to print collector's performance, uncomment '#define PRINT_SECOND_PERFORMANCE'
        /* second + recv_pkt/s + write/s */
        fprintf(fp_performance, "%d\t %d\t %d\n", sec_cnt, port_recv_int_cnt, write_cnt);

#ifdef SOCK_DA_TO_DL
        /* sw1-sw3 */
        fprintf(fp_int_info, "%d\t %d\t %d\t %ld\t %ld\t %d\t %f\t %d\t %ld\t %ld\t %d\t %f\t %d\t %ld\t %ld\t %d\t %f\t\n",
               sec_cnt, switch_map_info,
               flow_info.cur_pkt_info[2].switch_id, flow_info.cur_pkt_info[2].n_bytes, flow_info.cur_pkt_info[2].n_packets,
               flow_info.cur_pkt_info[2].hop_latency, flow_info.cur_pkt_info[2].bandwidth,
               flow_info.cur_pkt_info[1].switch_id, flow_info.cur_pkt_info[1].n_bytes, flow_info.cur_pkt_info[1].n_packets,
               flow_info.cur_pkt_info[1].hop_latency, flow_info.cur_pkt_info[1].bandwidth,
               flow_info.cur_pkt_info[0].switch_id, flow_info.cur_pkt_info[0].n_bytes, flow_info.cur_pkt_info[0].n_packets,
               flow_info.cur_pkt_info[0].hop_latency, flow_info.cur_pkt_info[0].bandwidth
               );
#endif

#ifdef SOCK_DA_TO_OCM
        /* sw1-sw3 */
        fprintf(fp_ber_arr, "%d\t %d\t %.16g\t %d\t %.16g\t %d\t %.16g\t\n",
                sec_cnt, flow_info.cur_pkt_info[2].switch_id, flow_info.cur_pkt_info[2].ber,
                flow_info.cur_pkt_info[1].switch_id, flow_info.cur_pkt_info[1].ber,
                flow_info.cur_pkt_info[0].switch_id, flow_info.cur_pkt_info[0].ber
                );
#endif

        if (sec_cnt % 10 == 0) {
#ifdef SOCK_DA_TO_DL
            fflush(fp_int_info);
            fflush(fp_norm_bd);
#endif

#ifdef SOCK_DA_TO_OCM
            fflush(fp_ber_arr);
#endif
        }
#endif

        fflush(fp_performance);
        fflush(stdout);
        write_cnt = 0;
        port_recv_int_cnt = 0;
    }

#ifdef SOCK_DA_TO_OCM   // if want to send 'ber' to OCM controller, uncomment '#define SOCK_DA_TO_OCM'
    for (i = 0; i < ttl; i++) {
        int cur_switch_id = flow_info.cur_pkt_info[i].switch_id - 1;  // array starts from 0

        /* now only monitor [0, OCM_COLLECTOR_COLLECTED_NODES) switches */
        if (cur_switch_id >= OCM_COLLECTOR_COLLECTED_NODES) {
            /* pass */
            continue;
        }

        ber_to_ocm_collector_info.ber[cur_switch_id] = flow_info.cur_pkt_info[i].ber;

        if (cur_switch_id == 0) {   // cur_switch_id = sw1
            cur_ber = flow_info.cur_pkt_info[i].ber;

            if ((cur_ber != his_ber) && (send_flag)) {
                memcpy(buf_send_ber, &ber_to_ocm_collector_info, sizeof(ber_to_ocm_collector_info_t));
                if ((send(clientfd_ocm, buf_send_ber, sizeof(ber_to_ocm_collector_info_t), 0)) < 0) {
                    send_flag = 0;
                    RTE_LOG(INFO, OCMSOCK, "client socket to OCM collector is closed.\n");
                }
                send_times++;
                printf("sec: %d, send_times:%d, ber[0]: %g\n", sec_cnt, send_times, cur_ber);
                his_ber = cur_ber;
                bzero(buf_send_ber, MAXLINE);
            }
        }
    }
#endif

#ifdef SOCK_DA_TO_DL  // if want to send 'bd_to_dl_info_t' to DL module, uncomment '#define SOCK_DA_TO_DL'

    /* tsf: after we collect bandwidth completely, we construct 'bd_to_dl_info_t' and send to DL module */
    for (i = 0; i < ttl; i++) {
        int cur_switch_id = flow_info.cur_pkt_info[i].switch_id - 1;  // array starts from 0

        /* now only monitor [0, DL_COLLECTED_NODES) switches */
        if (cur_switch_id >= DL_COLLECTED_NODES) {
            /* pass */
            continue;
        }

        /* filter redundant same bandwidth value. */
//        if (bd_win_info.cur_win_bd[cur_switch_id][bd_win_info.bd_win_num[cur_switch_id]] == 0) {  // init stage, bd_win_num = 0
        if (bd_win_info.cur_win_bd[cur_switch_id][bd_win_info.bd_win_num[cur_switch_id]] == 0) {  // init stage, bd_win_num = 0
            /* assign values. */
            bd_win_info.cur_win_bd[cur_switch_id][bd_win_info.bd_win_num[cur_switch_id]] = flow_info.cur_pkt_info[i].bandwidth;
            /*printf("init stage, cur_switch_id: %d, bd_win_num: %d, time_win_num: %d, bandwidth: %f.\n",
                   cur_switch_id, bd_win_info.bd_win_num[cur_switch_id], bd_win_info.global_time_win_num,
                   bd_win_info.cur_win_bd[cur_switch_id][bd_win_info.bd_win_num[cur_switch_id]]);*/
        }

        if (bd_win_info.cur_win_bd[cur_switch_id][bd_win_info.bd_win_num[cur_switch_id]] != flow_info.cur_pkt_info[i].bandwidth) { // filter stage
            /* slice window to next one, and update bandwidth info */
            bd_win_info.bd_win_num[cur_switch_id] += 1;
            bd_win_info.cur_win_bd[cur_switch_id][bd_win_info.bd_win_num[cur_switch_id]] = flow_info.cur_pkt_info[i].bandwidth;
            /*printf("filter stage, cur_switch_id: %d, bd_win_num: %d, time_win_num: %d, bandwidth: %f.\n",
                   cur_switch_id, bd_win_info.bd_win_num[cur_switch_id], bd_win_info.global_time_win_num,
                   bd_win_info.cur_win_bd[cur_switch_id][bd_win_info.bd_win_num[cur_switch_id]]);*/
        }
    }

    if (bd_win_info.sec_should_write && send_flag) {  // send bandwidth at second scale.

        get_mean_bandwidth_at_second_scale(&bd_win_info, &bd_to_dl_info);

        /* during ovs-pof's init stage, the first second's bandwidth is not exactly right. thus we repeat first second trace
         * for twice, and ignore the first second data. */
        if (bd_win_info.sec == 1) {
            goto reset_bd_win_info;
        }

        /* send data to DL module */
        memcpy(buf_send_bd, &bd_to_dl_info, sizeof(bd_to_dl_info_t));
        if ((send(clientfd_ocm, buf_send_bd, sizeof(bd_to_dl_info_t), 0)) < 0) {
            send_flag = 0;
            RTE_LOG(INFO, OCMSOCK, "client socket to DL module is closed.\n");
        }

        /* reset */
reset_bd_win_info:
        bzero(&bd_win_info, sizeof(bd_win_info_t));
        bzero(&bd_to_dl_info, sizeof(bd_to_dl_info_t));
        bzero(buf_send_bd, MAXLINE);
    }

#endif


    /* auto stop test. 'time_interval'=0 to disable to run. */
    if (timer_interval && (sec_cnt > timer_interval)) {  // 15s in default, -R [interval] to adjust
        signal_handler(SIGINT);
    }
}

static void
l2fwd_simple_forward(struct rte_mbuf *m, unsigned portid)
{
	struct ether_hdr *eth;
	void *tmp;
	unsigned dst_port;
	int sent;
	struct rte_eth_dev_tx_buffer *buffer;

	dst_port = l2fwd_dst_ports[portid];
	eth = rte_pktmbuf_mtod(m, struct ether_hdr *);

	/* 02:00:00:00:00:xx */
	tmp = &eth->d_addr.addr_bytes[0];
	*((uint64_t *)tmp) = 0x000000000002 + ((uint64_t)dst_port << 40);

	/* src addr */
	ether_addr_copy(&l2fwd_ports_eth_addr[dst_port], &eth->s_addr);

	buffer = tx_buffer[dst_port];
	sent = rte_eth_tx_buffer(dst_port, 0, buffer, m);
	if (sent)
		port_statistics[dst_port].tx += sent;
}

/* tsf: tcp sock thread to wait connect <one client at the same time>.
 *      this socket collect 'ber' data and then send to the ocm collector (OCM_Monotor_Collector_Ctrl.java).
 *      the ocm collector leverages 'ber' to adjust <request_frequency, request_slice_precision, etc>.
 * */
static int sock_process_ocm_thread() {
    char buf_recv_ber[MAXLINE] = {0};

    int serverfd;
    if ((serverfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        RTE_LOG(INFO, OCMSOCK, "create socket error.\n");
        return -1;
    }

    struct sockaddr_in server;
    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(SOCKET_OCM_PORT);
    inet_pton(AF_INET, SERVER_ADDR, &server.sin_addr);

    if (bind(serverfd, (struct sockaddr *) &server, sizeof(server)) < 0) {
        RTE_LOG(INFO, OCMSOCK, "bind socket error.\n");
        return -1;
    }

    if (listen(serverfd, PENDING_QUEUE) < 0) {
        RTE_LOG(INFO, OCMSOCK, "listen socket error.\n");
        return -1;
    }

#ifdef TEST_READ_TRACE_FROM_TXT_SEND_TO_DL   // if want to read trace form txt then send to DL module, uncomment '#define TEST_READ_TRACE_FROM_TXT_SEND_TO_DL'
    bd_to_dl_info_t trace_bd_to_dl_info = {0};

    FILE *fp = fopen("Traffic-test-001.txt", "r");   // Traffic-test.txt is rows of [16001, 170000] of Traffic.txt
//    int trainging_len = 160000;
//    int testing_len = 10000;
    if (fp == NULL) {
        RTE_LOG(INFO, OCMSOCK, "open file error.\n");
        return 0;
    }

    RTE_LOG(INFO, OCMSOCK, "traffic file opened successfully.\n");
#endif

    struct sockaddr_in client;
    socklen_t client_len = sizeof(struct sockaddr);
    while (true) {
        RTE_LOG(INFO, OCMSOCK, "server <%s> port <%d>, waiting client to be connected ...\n", SERVER_ADDR, SOCKET_OCM_PORT);

        clientfd_ocm = accept(serverfd, (struct sockaddr *) &client, &client_len);
        if (clientfd_ocm <= 0) {
            printf("accept error.\n");
            return -1;
        }

        RTE_LOG(INFO, OCMSOCK, "accept one client connection.\n");

#ifdef TEST_READ_TRACE_FROM_TXT_SEND_TO_DL // if want to read trace form txt then send to DL module, uncomment '#define TEST_READ_TRACE_FROM_TXT_SEND_TO_DL'
        rewind(fp);        // every sock reconnect, reset the fp to file's first line
#endif

        /* every reconnection, reset status. and only sock connects DA, send_flag turn 1 from 0. */
        send_flag = 1;
        send_times = 0;
        sec_cnt = 0;

#ifdef TEST_BER_SEND_TO_OCM_AGENT  // if want to send 'ber' to OCM agent to adjust ocm collection policy, uncomment '#define TEST_BER_SEND_TO_OCM_AGENT'
        char buf_send_ber[MAXLINE] = {0};
        while (send_flag) {
            double ber = 7.754045171636897e-05;
            memcpy(buf_send_ber, &ber, sizeof(ber));
            if ((send(clientfd_ocm, buf_send_ber, sizeof(ber), 0)) < 0) {
                send_flag = 0;
                break;
            }
            send_times++;
            sleep(1);
            bzero(buf_send_ber, MAXLINE);
            printf("server send:%d,  %.16g\n", send_times, ber);
        }
#endif

#ifdef TEST_READ_TRACE_FROM_TXT_SEND_TO_DL  // if want to read trace form txt then send to DL module, uncomment '#define TEST_READ_TRACE_FROM_TXT_SEND_TO_DL'
        char buf_send_trace[MAXLINE] = {0};
        float trace = 0.0;

        while (send_flag) {
            // if fp is at end of file (EOF), return back to first line
            if (fscanf(fp, "%f", &trace) == EOF) {
                rewind(fp);
            }

            /* send one float. */
//            memcpy(buf_send_trace, &trace, sizeof(trace));

            /* send float array. */
            trace_bd_to_dl_info.bandwidth[0] = trace;
            trace_bd_to_dl_info.bandwidth[1] = 1.1;
            trace_bd_to_dl_info.bandwidth[2] = 2.2;
            memcpy(buf_send_trace, &trace_bd_to_dl_info, sizeof(bd_to_dl_info_t));

            if ((send(clientfd_ocm, buf_send_trace, sizeof(bd_to_dl_info_t), 0)) < 0) {
                send_flag = 0;
                break;
            }

            send_times++;
            sleep(1);
            bzero(buf_send_trace, MAXLINE);
            bzero(&trace_bd_to_dl_info, sizeof(bd_to_dl_info_t));
            printf("server send:%d, %f\n", send_times, trace);
        }
#endif

    }
}

/* main processing loop */
static void
l2fwd_main_loop(void)
{
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	struct rte_mbuf *m;
	int sent;
	unsigned lcore_id;
	uint64_t prev_tsc, diff_tsc, cur_tsc, timer_tsc;
	unsigned i, j, portid, nb_rx;
	struct lcore_queue_conf *qconf;
	const uint64_t drain_tsc = (rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S *
			BURST_TX_DRAIN_US;
	struct rte_eth_dev_tx_buffer *buffer;

	prev_tsc = 0;
	timer_tsc = 0;

	lcore_id = rte_lcore_id();
	qconf = &lcore_queue_conf[lcore_id];

	if (qconf->n_rx_port == 0) {
		RTE_LOG(INFO, L2FWD, "lcore %u has nothing to do\n", lcore_id);
		return;
	}

	RTE_LOG(INFO, L2FWD, "entering main loop on lcore %u\n", lcore_id);

	for (i = 0; i < qconf->n_rx_port; i++) {

		portid = qconf->rx_port_list[i];
		RTE_LOG(INFO, L2FWD, " -- lcoreid=%u portid=%u\n", lcore_id,
			portid);

	}

	while (!force_quit) {

        if (SOCK_SHOULD_BE_RUN & BER_TCP_SOCK_CLIENT_RUN_ONCE) {
            RTE_LOG(INFO, OCMSOCK, "run here.\n");
            int ret = pthread_create(&tid_sock_process_ocm_thread, NULL, (void *) &sock_process_ocm_thread, NULL);
            if (ret == 0) {
//            pthread_join(tid_sock_process_ocm_thread, NULL);
                BER_TCP_SOCK_CLIENT_RUN_ONCE = false;
                RTE_LOG(INFO, OCMSOCK, "sock_process_ocm_thread start.\n");
            }
        }

		cur_tsc = rte_rdtsc();

		/*
		 * TX burst queue drain
		 */
		diff_tsc = cur_tsc - prev_tsc;
		if (unlikely(diff_tsc > drain_tsc)) {

			for (i = 0; i < qconf->n_rx_port; i++) {

				portid = l2fwd_dst_ports[qconf->rx_port_list[i]];
				buffer = tx_buffer[portid];

#ifdef L2_FWD_APP   // if want to run original l2fwd app, uncomment '#define L2_FWD'
				sent = rte_eth_tx_buffer_flush(portid, 0, buffer);
				if (sent)
					port_statistics[portid].tx += sent;
#endif

			}

			/* if timer is enabled */
			if (timer_period > 0) {

				/* advance the timer */
				timer_tsc += diff_tsc;

				/* if timer has reached its timeout */
				if (unlikely(timer_tsc >= timer_period)) {

					/* do this only on master core */
					if (lcore_id == rte_get_master_lcore()) {
#ifdef L2_FWD_APP   // if want to run original l2fwd app, uncomment '#define L2_FWD'
						print_stats();
#endif
						/* reset the timer */
						timer_tsc = 0;
					}
				}
			}

			prev_tsc = cur_tsc;
		}

		/*
		 * Read packet from RX queues
		 */
		for (i = 0; i < qconf->n_rx_port; i++) {

			portid = qconf->rx_port_list[i];
			nb_rx = rte_eth_rx_burst((uint8_t) portid, 0,
						 pkts_burst, MAX_PKT_BURST);

			port_statistics[portid].rx += nb_rx;

			for (j = 0; j < nb_rx; j++) {
				m = pkts_burst[j];
				rte_prefetch0(rte_pktmbuf_mtod(m, void *));
#ifdef L2_FWD_APP   // if want to run original l2fwd app, uncomment '#define L2_FWD'
				l2fwd_simple_forward(m, portid);
#endif
				process_int_pkt(m, portid);

                /* free the mbuf. */
                rte_pktmbuf_free(m);
			}
		}
	}
}

static int
l2fwd_launch_one_lcore(__attribute__((unused)) void *dummy)
{
	l2fwd_main_loop();
	return 0;
}

/* display usage */
static void
l2fwd_usage(const char *prgname)
{
	printf("%s [EAL options] -- -p PORTMASK [-q NQ]\n"
	       "  -p PORTMASK: hexadecimal bitmask of ports to configure\n"
	       "  -q NQ: number of queue (=ports) per lcore (default is 1)\n"
		   "  -T PERIOD: statistics will be refreshed each PERIOD seconds (0 to disable, 10 default, 86400 maximum)\n"
           "  -R INTERVAL: running INTERVAL seconds to quit INT packet processing (0 to disable, 15 default, 86400 maximum)\n"
           "  -S SOCKET flag (enter number > 1): run collector as socket server, periodically send data to client (now only support for 'ber')\n",
	       prgname);
}

static int
l2fwd_parse_portmask(const char *portmask)
{
	char *end = NULL;
	unsigned long pm;

	/* parse hexadecimal string */
	pm = strtoul(portmask, &end, 16);
	if ((portmask[0] == '\0') || (end == NULL) || (*end != '\0'))
		return -1;

	if (pm == 0)
		return -1;

	return pm;
}

static unsigned int
l2fwd_parse_nqueue(const char *q_arg)
{
	char *end = NULL;
	unsigned long n;

	/* parse hexadecimal string */
	n = strtoul(q_arg, &end, 10);
	if ((q_arg[0] == '\0') || (end == NULL) || (*end != '\0'))
		return 0;
	if (n == 0)
		return 0;
	if (n >= MAX_RX_QUEUE_PER_LCORE)
		return 0;

	return n;
}

static int
l2fwd_parse_timer_period(const char *q_arg)
{
	char *end = NULL;
	int n;

	/* parse number string */
	n = strtol(q_arg, &end, 10);
	if ((q_arg[0] == '\0') || (end == NULL) || (*end != '\0'))
		return -1;
	if (n >= MAX_TIMER_PERIOD)
		return -1;

	return n;
}


static int
l2fwd_parse_timer_interval(const char *q_arg)
{
    char *end = NULL;
    int n;

    /* parse number string */
    n = strtol(q_arg, &end, 10);
    if ((q_arg[0] == '\0') || (end == NULL) || (*end != '\0'))
        return -1;
    if (n >= MAX_TIMER_PERIOD)
        return -1;

    return n;
}


static int
l2fwd_parse_sock_flag(const char *q_arg)
{
    char *end = NULL;
    int n;

    /* parse number string */
    n = strtol(q_arg, &end, 10);
    if ((q_arg[0] == '\0') || (end == NULL) || (*end != '\0'))
        return -1;

    return n;
}

/* Parse the argument given in the command line of the application */
static int
l2fwd_parse_args(int argc, char **argv)
{
	int opt, ret, timer_secs, sock_port;
	int sock_should_be_run;
	char **argvopt;
	int option_index;
	char *prgname = argv[0];
	static struct option lgopts[] = {
		{NULL, 0, 0, 0}
	};

	argvopt = argv;

	while ((opt = getopt_long(argc, argvopt, "p:q:T:R:S:",
				  lgopts, &option_index)) != EOF) {

		switch (opt) {
		/* portmask */
		case 'p':
			l2fwd_enabled_port_mask = l2fwd_parse_portmask(optarg);
			if (l2fwd_enabled_port_mask == 0) {
				printf("invalid portmask\n");
				l2fwd_usage(prgname);
				return -1;
			}
			break;

		/* nqueue */
		case 'q':
			l2fwd_rx_queue_per_lcore = l2fwd_parse_nqueue(optarg);
			if (l2fwd_rx_queue_per_lcore == 0) {
				printf("invalid queue number\n");
				l2fwd_usage(prgname);
				return -1;
			}
			break;

		/* timer period */
		case 'T':
			timer_secs = l2fwd_parse_timer_period(optarg);
			if (timer_secs < 0) {
				printf("invalid timer period\n");
				l2fwd_usage(prgname);
				return -1;
			}
			timer_period = timer_secs;
			break;

        case 'R':
            timer_secs = l2fwd_parse_timer_interval(optarg);
            if (timer_secs < 0) {
                printf("invalid timer period\n");
                l2fwd_usage(prgname);
                return -1;
            }
            timer_interval = timer_secs;
            break;

        case 'S':
            sock_should_be_run = l2fwd_parse_sock_flag(optarg);
            if (sock_should_be_run < 0) {
                printf("sock_should_be_run set failed (enter number > 1)\n");
                l2fwd_usage(prgname);
                return -1;
            }
            SOCK_SHOULD_BE_RUN = sock_should_be_run;
            break;

		/* long options */
		case 0:
			l2fwd_usage(prgname);
			return -1;

		default:
			l2fwd_usage(prgname);
			return -1;
		}
	}

	if (optind >= 0)
		argv[optind-1] = prgname;

	ret = optind-1;
	optind = 0; /* reset getopt lib */
	return ret;
}

/* Check the link status of all ports in up to 9s, and print them finally */
static void
check_all_ports_link_status(uint8_t port_num, uint32_t port_mask)
{
#define CHECK_INTERVAL 100 /* 100ms */
#define MAX_CHECK_TIME 90 /* 9s (90 * 100ms) in total */
	uint8_t portid, count, all_ports_up, print_flag = 0;
	struct rte_eth_link link;

	printf("\nChecking link status");
	fflush(stdout);
	for (count = 0; count <= MAX_CHECK_TIME; count++) {
		if (force_quit)
			return;
		all_ports_up = 1;
		for (portid = 0; portid < port_num; portid++) {
			if (force_quit)
				return;
			if ((port_mask & (1 << portid)) == 0)
				continue;
			memset(&link, 0, sizeof(link));
			rte_eth_link_get_nowait(portid, &link);
			/* print link status if flag set */
			if (print_flag == 1) {
				if (link.link_status)
					printf("Port %d Link Up - speed %u "
						"Mbps - %s\n", (uint8_t)portid,
						(unsigned)link.link_speed,
				(link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
					("full-duplex") : ("half-duplex\n"));
				else
					printf("Port %d Link Down\n",
						(uint8_t)portid);
				continue;
			}
			/* clear all_ports_up flag if any link down */
			if (link.link_status == ETH_LINK_DOWN) {
				all_ports_up = 0;
				break;
			}
		}
		/* after finally printing all link status, get out */
		if (print_flag == 1)
			break;

		if (all_ports_up == 0) {
			printf(".");
			fflush(stdout);
			rte_delay_ms(CHECK_INTERVAL);
		}

		/* set the print_flag if all ports up or timeout */
		if (all_ports_up == 1 || count == (MAX_CHECK_TIME - 1)) {
			print_flag = 1;
			printf("done\n");
		}
	}
}

static void
signal_handler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		printf("\n\nSignal %d received, preparing to exit...\n",
				signum);
		force_quit = true;

		fflush(stdout);

		/* flush file and close them. */
        fflush(fp_performance);
        fclose(fp_performance);

#ifdef SOCK_DA_TO_DL
		fflush(fp_int_info);
		fflush(fp_norm_bd);
        fclose(fp_int_info);
        fclose(fp_norm_bd);
#endif

#ifdef SOCK_DA_TO_OCM
        fflush(fp_ber_arr);
        fclose(fp_ber_arr);
#endif
	}
}

int
main(int argc, char **argv)
{
	struct lcore_queue_conf *qconf;
	struct rte_eth_dev_info dev_info;
	int ret;
	uint8_t nb_ports;
	uint8_t nb_ports_available;
	uint8_t portid, last_port;
	unsigned lcore_id, rx_lcore_id;
	unsigned nb_ports_in_mask = 0;

	/* init EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid EAL arguments\n");
	argc -= ret;
	argv += ret;

	/* init files */
#ifdef SOCK_DA_TO_DL
    fp_performance = fopen("result_packet_exp_second_performance.txt", "w+");
	fp_int_info = fopen("result_packet_exp_int_info.txt", "w+");
	fp_norm_bd = fopen("result_packet_exp_second_normalized_bandwidth.txt", "w+");
#endif

#ifdef SOCK_DA_TO_OCM
    fp_performance = fopen("result_optical_exp_second_performance.txt", "w+");
    fp_ber_arr = fopen("result_optical_exp_ber_arr.txt", "w+");
#endif

	force_quit = false;
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	/* parse application arguments (after the EAL ones) */
	ret = l2fwd_parse_args(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid L2FWD arguments\n");

	/* convert to number of cycles */
	timer_period *= rte_get_timer_hz();

	/* create the mbuf pool */
	l2fwd_pktmbuf_pool = rte_pktmbuf_pool_create("mbuf_pool", NB_MBUF,
		MEMPOOL_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE,
		rte_socket_id());
	if (l2fwd_pktmbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot init mbuf pool\n");

	nb_ports = rte_eth_dev_count();
	if (nb_ports == 0)
		rte_exit(EXIT_FAILURE, "No Ethernet ports - bye\n");

	/* reset l2fwd_dst_ports */
	for (portid = 0; portid < RTE_MAX_ETHPORTS; portid++)
		l2fwd_dst_ports[portid] = 0;
	last_port = 0;

	/*
	 * Each logical core is assigned a dedicated TX queue on each port.
	 */
	for (portid = 0; portid < nb_ports; portid++) {
		/* skip ports that are not enabled */
		if ((l2fwd_enabled_port_mask & (1 << portid)) == 0)
			continue;

		if (nb_ports_in_mask % 2) {
			l2fwd_dst_ports[portid] = last_port;
			l2fwd_dst_ports[last_port] = portid;
		}
		else
			last_port = portid;

		nb_ports_in_mask++;

		rte_eth_dev_info_get(portid, &dev_info);
	}
	if (nb_ports_in_mask % 2) {
		printf("Notice: odd number of ports in portmask.\n");
		l2fwd_dst_ports[last_port] = last_port;
	}

	rx_lcore_id = 0;
	qconf = NULL;

	/* Initialize the port/queue configuration of each logical core */
	for (portid = 0; portid < nb_ports; portid++) {
		/* skip ports that are not enabled */
		if ((l2fwd_enabled_port_mask & (1 << portid)) == 0)
			continue;

		/* get the lcore_id for this port */
		while (rte_lcore_is_enabled(rx_lcore_id) == 0 ||
		       lcore_queue_conf[rx_lcore_id].n_rx_port ==
		       l2fwd_rx_queue_per_lcore) {
			rx_lcore_id++;
			if (rx_lcore_id >= RTE_MAX_LCORE)
				rte_exit(EXIT_FAILURE, "Not enough cores\n");
		}

		if (qconf != &lcore_queue_conf[rx_lcore_id])
			/* Assigned a new logical core in the loop above. */
			qconf = &lcore_queue_conf[rx_lcore_id];

		qconf->rx_port_list[qconf->n_rx_port] = portid;
		qconf->n_rx_port++;
		printf("Lcore %u: RX port %u\n", rx_lcore_id, (unsigned) portid);
	}

	nb_ports_available = nb_ports;

	/* Initialise each port */
	for (portid = 0; portid < nb_ports; portid++) {
		/* skip ports that are not enabled */
		if ((l2fwd_enabled_port_mask & (1 << portid)) == 0) {
			printf("Skipping disabled port %u\n", (unsigned) portid);
			nb_ports_available--;
			continue;
		}
		/* init port */
		printf("Initializing port %u... ", (unsigned) portid);
		fflush(stdout);
		ret = rte_eth_dev_configure(portid, 1, 1, &port_conf);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "Cannot configure device: err=%d, port=%u\n",
				  ret, (unsigned) portid);

		rte_eth_macaddr_get(portid,&l2fwd_ports_eth_addr[portid]);

		/* init one RX queue */
		fflush(stdout);
		ret = rte_eth_rx_queue_setup(portid, 0, nb_rxd,
					     rte_eth_dev_socket_id(portid),
					     NULL,
					     l2fwd_pktmbuf_pool);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "rte_eth_rx_queue_setup:err=%d, port=%u\n",
				  ret, (unsigned) portid);

		/* init one TX queue on each port */
		fflush(stdout);
		ret = rte_eth_tx_queue_setup(portid, 0, nb_txd,
				rte_eth_dev_socket_id(portid),
				NULL);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup:err=%d, port=%u\n",
				ret, (unsigned) portid);

		/* Initialize TX buffers */
		tx_buffer[portid] = rte_zmalloc_socket("tx_buffer",
				RTE_ETH_TX_BUFFER_SIZE(MAX_PKT_BURST), 0,
				rte_eth_dev_socket_id(portid));
		if (tx_buffer[portid] == NULL)
			rte_exit(EXIT_FAILURE, "Cannot allocate buffer for tx on port %u\n",
					(unsigned) portid);

		rte_eth_tx_buffer_init(tx_buffer[portid], MAX_PKT_BURST);

		ret = rte_eth_tx_buffer_set_err_callback(tx_buffer[portid],
				rte_eth_tx_buffer_count_callback,
				&port_statistics[portid].dropped);
		if (ret < 0)
				rte_exit(EXIT_FAILURE, "Cannot set error callback for "
						"tx buffer on port %u\n", (unsigned) portid);

		/* Start device */
		ret = rte_eth_dev_start(portid);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "rte_eth_dev_start:err=%d, port=%u\n",
				  ret, (unsigned) portid);

		printf("done: \n");

		rte_eth_promiscuous_enable(portid);

		printf("Port %u, MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n\n",
				(unsigned) portid,
				l2fwd_ports_eth_addr[portid].addr_bytes[0],
				l2fwd_ports_eth_addr[portid].addr_bytes[1],
				l2fwd_ports_eth_addr[portid].addr_bytes[2],
				l2fwd_ports_eth_addr[portid].addr_bytes[3],
				l2fwd_ports_eth_addr[portid].addr_bytes[4],
				l2fwd_ports_eth_addr[portid].addr_bytes[5]);

		/* initialize port stats */
		memset(&port_statistics, 0, sizeof(port_statistics));
	}

	if (!nb_ports_available) {
		rte_exit(EXIT_FAILURE,
			"All available ports are disabled. Please set portmask.\n");
	}

	check_all_ports_link_status(nb_ports, l2fwd_enabled_port_mask);

	ret = 0;
	/* launch per-lcore init on every lcore */
	rte_eal_mp_remote_launch(l2fwd_launch_one_lcore, NULL, CALL_MASTER);
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		if (rte_eal_wait_lcore(lcore_id) < 0) {
			ret = -1;
			break;
		}
	}

	for (portid = 0; portid < nb_ports; portid++) {
		if ((l2fwd_enabled_port_mask & (1 << portid)) == 0)
			continue;
		printf("Closing port %d...", portid);
		rte_eth_dev_stop(portid);
		rte_eth_dev_close(portid);
		printf(" Done\n");
	}
	printf("Bye...\n");

	return ret;
}
