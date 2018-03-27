/* for I/O module def'ns */
#include "io_module.h"
/* for num_devices decl */
#include "config.h"
/* std lib funcs */
#include <stdlib.h>
/* std io funcs */
#include <stdio.h>
/* strcmp func etc. */
#include <string.h>
/* for ifreq struct */
#include <net/if.h>
/* for ioctl */
#include <sys/ioctl.h>
#ifndef DISABLE_DPDK
/* for dpdk ethernet functions (get mac addresses) */
#include <rte_ethdev.h>
#endif
/* for TRACE_* */
#include "debug.h"
/* for inet_* */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
/* for getopt() */
#include <unistd.h>
/* for getifaddrs */
#include <sys/types.h>
#include <ifaddrs.h>
/*----------------------------------------------------------------------------*/
io_module_func *current_iomodule_func = &dpdk_module_func;
#ifndef DISABLE_DPDK
enum rte_proc_type_t eal_proc_type_detect(void);
#endif
/*----------------------------------------------------------------------------*/
#define ALL_STRING			"all"
#define MAX_PROCLINE_LEN		1024
#define MAX(a, b) 			((a)>(b)?(a):(b))
#define MIN(a, b) 			((a)<(b)?(a):(b))
/*----------------------------------------------------------------------------*/
#ifndef DISABLE_PSIO
static int
GetNumQueues()
{
	FILE *fp;
	char buf[MAX_PROCLINE_LEN];
	int queue_cnt;

	fp = fopen("/proc/interrupts", "r");
	if (!fp) {
		TRACE_CONFIG("Failed to read data from /proc/interrupts!\n");
		return -1;
	}

	/* count number of NIC queues from /proc/interrupts */
	queue_cnt = 0;
	while (!feof(fp)) {
		if (fgets(buf, MAX_PROCLINE_LEN, fp) == NULL)
			break;

		/* "xge0-rx" is the keyword for counting queues */
		if (strstr(buf, "xge0-rx")) {
			queue_cnt++;
		}
	}
	fclose(fp);

	return queue_cnt;
}
#endif /* !PSIO */
/*----------------------------------------------------------------------------*/
int
SetInterfaceInfo(char* dev_name_list)
{
	int eidx = 0;
	int i, j;

	int set_all_inf = (strncmp(dev_name_list, ALL_STRING, sizeof(ALL_STRING))==0);

	TRACE_CONFIG("Loading interface setting\n");

	CONFIG.eths = (struct eth_table *)
			calloc(MAX_DEVICES, sizeof(struct eth_table));
	if (!CONFIG.eths) {
		TRACE_ERROR("Can't allocate space for CONFIG.eths\n");
		exit(EXIT_FAILURE);
	}

	if (current_iomodule_func == &ps_module_func) {
	} else if (current_iomodule_func == &dpdk_module_func)
	{
		int cpu = CONFIG.num_cores;
		uint32_t cpumask = 0;
		char cpumaskbuf[10];
		char mem_channels[5];
		int ret;
		static struct ether_addr ports_eth_addr[RTE_MAX_ETHPORTS];

		/* get the cpu mask */
		for (ret = 0; ret < cpu; ret++)
			cpumask = (cpumask | (1 << ret));
		sprintf(cpumaskbuf, "%X", cpumask);

		/* get the mem channels per socket */
		if (CONFIG.num_mem_ch == 0) {
			TRACE_ERROR("DPDK module requires # of memory channels "
				    "per socket parameter!\n");
			exit(EXIT_FAILURE);
		}
		sprintf(mem_channels, "%d", CONFIG.num_mem_ch);

		/* initialize the rte env first, what a waste of implementation effort!  */
		char *argv[] = {"",
				"-c",
				cpumaskbuf,
				"-n",
				mem_channels,
				"--proc-type=auto",
				""
		};
		const int argc = 6;

		/*
		 * re-set getopt extern variable optind.
		 * this issue was a bitch to debug
		 * rte_eal_init() internally uses getopt() syscall
		 * mtcp applications that also use an `external' getopt
		 * will cause a violent crash if optind is not reset to zero
		 * prior to calling the func below...
		 * see man getopt(3) for more details
		 */
		optind = 0;

		/* initialize the dpdk eal env */
		ret = rte_eal_init(argc, argv);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "Invalid EAL args!\n");
		/* give me the count of 'detected' ethernet ports */
		num_devices = rte_eth_dev_count();
		if (num_devices == 0) {
			rte_exit(EXIT_FAILURE, "No Ethernet port!\n");
		}

		/* get mac addr entries of 'detected' dpdk ports */
		for (ret = 0; ret < num_devices; ret++)
			rte_eth_macaddr_get(ret, &ports_eth_addr[ret]);

		/* HARD CODE DPDK PORT CONFIG */
		char* dpdk_dev_name = "dpdk0";
		const char* dpdk_dev_ip_addresss = "10.19.19.119";
		const char* dpdk_dev_netmask = "255.255.255.0";

		CONFIG.eths_num = 1;
		strcpy(CONFIG.eths[0].dev_name, dpdk_dev_name);

		uint32_t ip_addr;

		inet_pton(AF_INET, dpdk_dev_ip_address, &ip_addr);
		CONFIG.eths[0].ip_addr = ip_addr;

		inet_pton(AF_INET, dpdk_dev_netmask, &ip_addr);
		CONFIG.eths[0].netmask = ip_addr;

		memcpy(&CONFIG.eths[0].haddr[0], &ports_eth_addr[0], ETH_ALEN);

		CONFIG.eths[0].ifindex = 0;

		/* END */

		num_queues = MIN(CONFIG.num_cores, MAX_CPUS);

		/* check if process is primary or secondary */
		CONFIG.multi_process_is_master = (eal_proc_type_detect() == RTE_PROC_PRIMARY) ? 1 : 0;
	}

	CONFIG.nif_to_eidx = (int*)calloc(MAX_DEVICES, sizeof(int));

	if (!CONFIG.nif_to_eidx) {
	        exit(EXIT_FAILURE);
	}

	for (i = 0; i < MAX_DEVICES; ++i) {
	        CONFIG.nif_to_eidx[i] = -1;
	}

	for (i = 0; i < CONFIG.eths_num; ++i) {

		j = CONFIG.eths[i].ifindex;
		if (j >= MAX_DEVICES) {
		        TRACE_ERROR("ifindex of eths_%d exceed the limit: %d\n", i, j);
		        exit(EXIT_FAILURE);
		}

		/* the physic port index of the i-th port listed in the config file is j*/
		CONFIG.nif_to_eidx[j] = i;
	}

	return 0;
}
/*----------------------------------------------------------------------------*/
int
FetchEndianType()
{
#ifndef DISABLE_DPDK
	char *argv;
	char **argp = &argv;
	/* dpdk_module_func logic down below */
	dpdk_module_func.dev_ioctl(NULL, CONFIG.eths[0].ifindex, DRV_NAME, (void *)argp);
	if (!strcmp(*argp, "net_i40e"))
		return 1;

	return 0;
#else
	return 1;
#endif
}
/*----------------------------------------------------------------------------*/
