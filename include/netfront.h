#include <mini-os/wait.h>
#include <mini-os/semaphore.h>
#ifdef CONFIG_NOXS
#include <mini-os/noxs.h>
#endif
#ifdef HAVE_LWIP
#include <lwip/netif.h>
#include <lwip/netif/etharp.h>
#endif
#include <xen/io/netif.h>

#if defined CONFIG_NETFRONT_LWIP_ONLY && !defined HAVE_LWIP
#error "netfront: Cannot build netfront purely for lwIP without having lwIP"
#endif

#define NET_TX_RING_SIZE __CONST_RING_SIZE(netif_tx, PAGE_SIZE)
#define NET_RX_RING_SIZE __CONST_RING_SIZE(netif_rx, PAGE_SIZE)

#ifndef CONFIG_NETFRONT_PERSISTENT_GRANTS
#if !defined CONFIG_NETFRONT_RX_BUFFERS || CONFIG_NETFRONT_RX_BUFFERS < 20
#define NET_RX_BUFFERS NET_RX_RING_SIZE
#else
#define NET_RX_BUFFERS CONFIG_NETFRONT_RX_BUFFERS
#endif
#endif

struct netfront_dev;

struct net_txbuffer {
#if defined CONFIG_NETFRONT_PERSISTENT_GRANTS || !defined CONFIG_NETFRONT_LWIP_ONLY
	void* page;
#endif
	grant_ref_t gref;
#ifdef HAVE_LWIP
	struct pbuf *pbuf;
#endif
};


struct net_rxbuffer {
	void* page;
	grant_ref_t gref;
#ifndef CONFIG_NETFRONT_PERSISTENT_GRANTS
	unsigned short id;
#ifdef HAVE_LWIP
	struct netfront_dev *dev;
	struct pbuf_custom cpbuf;
#endif
#endif
};

struct netfront_dev {
	domid_t dom;
	struct netfrontif *netif;

	unsigned short tx_freelist[NET_TX_RING_SIZE + 1];
	struct semaphore tx_sem;

	struct net_txbuffer tx_buffers[NET_TX_RING_SIZE];
#ifdef CONFIG_NETFRONT_PERSISTENT_GRANTS
	struct net_rxbuffer rx_buffers[NET_RX_RING_SIZE];
#else
	struct net_rxbuffer *rx_buffers[NET_RX_RING_SIZE];

	struct net_rxbuffer rx_buffer_pool[NET_RX_BUFFERS];
	unsigned short rx_freelist[NET_RX_BUFFERS + 1];
	unsigned short rx_avail;
#endif

	struct netif_tx_front_ring tx;
	struct netif_rx_front_ring rx;

	/* inflight response to be handled */
	struct netif_rx_response rsp;
	/* extras (if any) of the inflight buffer */
	struct netif_extra_info extras[XEN_NETIF_EXTRA_TYPE_MAX - 1];
	/* used by pbuf_copy_bits */
	struct pbuf *pbuf_cur;
#ifdef CONFIG_NETFRONT_PERSISTENT_GRANTS
	uint32_t pbuf_off;
#endif

	grant_ref_t tx_ring_ref;
	grant_ref_t rx_ring_ref;

	evtchn_port_t tx_evtchn;
	evtchn_port_t rx_evtchn;

#ifdef CONFIG_NOXS
	char *name;
	noxs_vif_ctrl_page_t *vif_page;
	struct noxs_dev_handle noxs_dev_handle;
#else
	char *nodename;
	char *backend;
	char *mac;

	xenbus_event_queue events;
#endif

#ifdef CONFIG_NETMAP
	int netmap;
	void *na;
#endif

#ifdef HAVE_LIBC
	int fd;
	unsigned char *data;
	size_t len;
	size_t rlen;
#endif

#ifdef HAVE_LWIP
	void (*netif_rx_pbuf)(struct pbuf *p, void *arg);
#endif
	void (*netif_rx)(unsigned char* data, int len, void *arg);
	void *netif_rx_arg;

#ifdef CONFIG_NETFRONT_STATS
	uint64_t txpkts;
	uint64_t txbytes;
#endif
};

void netfront_rx(struct netfront_dev *dev);
#define network_rx(dev) netfront_rx(dev);
#ifndef CONFIG_NETFRONT_LWIP_ONLY
void netfront_set_rx_handler(struct netfront_dev *dev, void (*thenetif_rx)(unsigned char* data, int len, void *arg), void *arg);
void netfront_xmit(struct netfront_dev *dev, unsigned char* data, int len);
#endif
struct netfront_dev *init_netfront(void *store_id, void (*netif_rx)(unsigned char *data, int len, void *arg), unsigned char rawmac[6], void *ip);
#ifdef HAVE_LWIP
void netfront_set_rx_pbuf_handler(struct netfront_dev *dev, void (*thenetif_rx)(struct pbuf *p, void *arg), void *arg);
err_t netfront_xmit_pbuf(struct netfront_dev *dev, struct pbuf *p, int co_type, int push);
void netfront_xmit_push(struct netfront_dev *dev);
#endif
void shutdown_netfront(struct netfront_dev *dev);
void suspend_netfront(void);
void resume_netfront(void);
void netfrontif_thread_suspend(struct netfrontif *nfi);
void netfrontif_thread_resume(struct netfrontif *nfi);
#ifdef HAVE_LIBC
int netfront_tap_open(char *nodename);
#ifndef CONFIG_NETFRONT_LWIP_ONLY
ssize_t netfront_receive(struct netfront_dev *dev, unsigned char *data, size_t len);
#endif
#endif

extern struct wait_queue_head netfront_queue;

#ifdef HAVE_LWIP
struct eth_addr *netfront_get_hwaddr(struct netfront_dev *dev, struct eth_addr *out);

#if defined CONFIG_START_NETWORK || defined CONFIG_INCLUDE_START_NETWORK
/* Call this to bring up the netfront interface and the lwIP stack.
 * N.B. _must_ be called from a thread; it's not safe to call this from 
 * app_main(). */
void start_networking(void);
void stop_networking(void);
#ifdef CONFIG_LWIP_NOTHREADS
/* Note: DHCP is not yet supported when CONFIG_LWIP_NOTHREADS is set */
void poll_networking(void);
#endif

void networking_set_addr(struct ip_addr *ipaddr, struct ip_addr *netmask, struct ip_addr *gw);
#endif
#endif

#ifdef CONFIG_SELECT_POLL
int netfront_get_fd(struct netfront_dev *dev);
#endif

#ifdef CONFIG_NETFRONT_STATS
void netfront_reset_txcounters(struct netfront_dev *dev);
void netfront_get_txcounters(struct netfront_dev *dev, uint64_t *out_txpkts, uint64_t *out_txbytes);
#endif

/*
 * STORE FUNCTIONS
 */
int netfront_store_dev_matches_id(struct netfront_dev *dev, void *store_id);
const char *netfront_store_dev_name(struct netfront_dev *dev);

int netfront_store_pre(struct netfront_dev *dev, void *store_id);
void netfront_store_post(struct netfront_dev *dev);

int netfront_store_init(struct netfront_dev *dev, int *is_split_evtchn);
void netfront_store_fini(struct netfront_dev *dev);

int netfront_store_front_data_create(struct netfront_dev *dev, int split_evtchn);
int netfront_store_front_data_destroy(struct netfront_dev *dev);
int netfront_store_wait_be_connect(struct netfront_dev *dev);
int netfront_store_wait_be_disconnect(struct netfront_dev *dev);
int netfront_store_read_mac(struct netfront_dev *dev, unsigned char rawmac[6]);
void netfront_store_read_ip(struct netfront_dev *dev, void *out);
