/* net.c — minimal network stack: Ethernet + ARP + IPv4 + ICMP
 *
 * Konfigurasi statis (QEMU SLIRP user networking):
 *   Guest IP  : 10.0.2.15
 *   Gateway IP: 10.0.2.2
 * MAC dibaca dari RTL8139 hardware setelah init.
 *
 * Alur ping:
 *   1. ARP request broadcast → tunggu ARP reply dari target
 *   2. Kirim ICMP echo request (type 8) ke target
 *   3. Poll rtl8139_recv(), proses paket masuk
 *   4. Jika dapat ICMP echo reply (type 0) dengan ID+seq cocok → cetak RTT
 */
#include "net.h"
#include "rtl8139.h"
#include <stdint.h>

extern void     print(const char *s);
extern void     itoa(uint32_t n, char *buf);
extern uint32_t get_ticks(void);
extern void     set_color(uint32_t fg, uint32_t bg);

/* ---- Konfigurasi jaringan statis ---- */
static uint8_t my_mac[6];
static const uint8_t MY_IP[4]    = {10, 0, 2, 15};

/* Fondasi AP forward declaration */
static void ip6_make_link_local(void);
static const uint8_t GW_IP[4]    = {10, 0, 2,  2};
static const uint8_t BCAST_MAC[6]= {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

/* ---- ARP cache (IP→MAC, 8 slot) ---- */
#define ARP_CACHE  8
static struct { uint8_t ip[4]; uint8_t mac[6]; int valid; } arp_tbl[ARP_CACHE];

/* ---- Buffer build paket ---- */
static uint8_t pkt_buf[1514];

/* ICMP identifier unik supaya tidak tertukar dengan proses lain */
static const uint16_t ICMP_ID = 0x4D59;  /* 'MY' */
static uint16_t ip_id_counter = 1;

/* ================================================================== */
/* TCP/UDP — structures & state constants                              */
/* ================================================================== */
#define TCP_CLOSED       0
#define TCP_SYN_SENT     1
#define TCP_ESTABLISHED  2
#define TCP_FIN_WAIT1    3
#define TCP_FIN_WAIT2    4
#define TCP_CLOSE_WAIT   5
#define TCP_LAST_ACK     6
#define TCP_TIME_WAIT    7

#define TCP_FIN  0x01
#define TCP_SYN  0x02
#define TCP_RST  0x04
#define TCP_PSH  0x08
#define TCP_ACK  0x10

#define TCP_RX_SIZE   4096  /* RX ring buffer per connection   */
#define TCP_TX_SIZE   1400  /* retransmit buffer per connection*/
#define TCP_MAX_CONN  4     /* simultaneous TCP connections    */
#define TCP_RTO       200   /* retransmit timeout (ms)         */
#define TCP_MAX_RETRANS 5   /* max retransmit attempts → RST   */
#define TCP_KEEPALIVE_IDLE  30000  /* idle ms sebelum keepalive probe */
#define TCP_KEEPALIVE_INTVL 5000   /* interval antar probe (ms)       */
#define TCP_KEEPALIVE_CNT   3      /* max probe sebelum close (ms)    */

typedef struct {
    uint8_t  used;
    uint8_t  state;
    uint8_t  dst_ip[4];
    uint8_t  dst_mac[6];
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t snd_nxt;       /* our next send sequence number   */
    uint32_t rcv_nxt;       /* expected next byte from remote  */
    uint8_t  rx[TCP_RX_SIZE];
    uint16_t rx_head;
    uint16_t rx_tail;
    uint8_t  fin_recv;      /* 1 = remote sent FIN             */
    uint8_t  rst_recv;      /* 1 = remote sent RST (refused)   */
    /* F-X1: retransmit */
    uint8_t  tx_buf[TCP_TX_SIZE]; /* last unacked data payload  */
    uint16_t tx_len;        /* bytes in tx_buf (0=nothing unacked)     */
    uint32_t tx_seq;        /* seq number of tx_buf[0]                 */
    uint32_t retrans_tick;  /* get_ticks() saat tx_buf terisi          */
    uint8_t  retrans_count; /* jumlah retransmit sudah dilakukan       */
    /* F-X2: out-of-order slot (1 packet) */
    uint8_t  ooo_buf[TCP_TX_SIZE]; /* 1 out-of-order segment             */
    uint16_t ooo_len;       /* bytes in ooo_buf (0=kosong)             */
    uint32_t ooo_seq;       /* seq number ooo_buf[0]                   */
    /* F-X3: keepalive */
    uint32_t last_rx_tick;  /* get_ticks() saat terakhir terima data   */
    uint8_t  ka_probes;     /* jumlah keepalive probe terkirim         */
    uint32_t ka_next_tick;  /* waktu probe berikutnya                  */
} TcpConn;

static TcpConn tcp_conns[TCP_MAX_CONN];
static uint16_t tcp_port_ctr = 49152;  /* ephemeral port counter */

/* Forward declarations needed by listen/accept code below */
static int  net_process(const uint8_t *pkt, uint16_t len,
                        const uint8_t *expect_src, uint16_t *rtt_out,
                        uint32_t send_tick);
static void tcp_tx(TcpConn *c, uint8_t flags, uint32_t seq, uint32_t ack,
                   const uint8_t *data, uint16_t dlen);

/* ================================================================
 * Fondasi AM — TCP Listen / Accept (server side)
 * ================================================================ */
#define TCP_LISTEN_SLOTS  2   /* max simultan server socket */

typedef struct {
    uint8_t  active;          /* 1 = slot ini listening */
    uint16_t port;            /* port yang di-listen */
    /* Accept queue: simpan sampai 2 koneksi masuk yang belum di-accept */
    int8_t   accept_q[2];     /* index ke tcp_conns, -1 = kosong */
    uint8_t  aq_head;
    uint8_t  aq_tail;
    uint8_t  aq_count;
} TcpListen;

static TcpListen tcp_listen[TCP_LISTEN_SLOTS];

/* net_tcp_listen(port): mulai listen di port.
 * Return listen_id (0..TCP_LISTEN_SLOTS-1), atau -1 jika gagal. */
int net_tcp_listen(uint16_t port) {
    int i;
    for (i = 0; i < TCP_LISTEN_SLOTS; i++) {
        if (!tcp_listen[i].active) {
            tcp_listen[i].active   = 1;
            tcp_listen[i].port     = port;
            tcp_listen[i].aq_head  = 0;
            tcp_listen[i].aq_tail  = 0;
            tcp_listen[i].aq_count = 0;
            tcp_listen[i].accept_q[0] = -1;
            tcp_listen[i].accept_q[1] = -1;
            return i;
        }
    }
    return -1;
}

/* net_tcp_accept(listen_id, timeout_ms): tunggu koneksi masuk.
 * Return conn_id sukses, -1 timeout, -2 invalid. */
int net_tcp_accept(int lid, uint32_t timeout_ms) {
    if (lid < 0 || lid >= TCP_LISTEN_SLOTS || !tcp_listen[lid].active) return -2;
    uint32_t deadline = get_ticks() + timeout_ms;
    while (get_ticks() < deadline) {
        /* Poll paket masuk */
        uint8_t pkt2[1514]; uint16_t plen;
        if (rtl8139_recv(pkt2, &plen) == 0 && plen > 54) {
            /* net_process menangani SYN dan membuat koneksi baru */
            net_process(pkt2, plen, 0, 0, 0);
        }
        if (tcp_listen[lid].aq_count > 0) {
            int8_t cid = tcp_listen[lid].accept_q[tcp_listen[lid].aq_head];
            tcp_listen[lid].aq_head = (tcp_listen[lid].aq_head + 1) % 2;
            tcp_listen[lid].aq_count--;
            return (int)cid;
        }
    }
    return -1;
}

/* net_tcp_unlisten(listen_id): berhenti listen */
void net_tcp_unlisten(int lid) {
    if (lid >= 0 && lid < TCP_LISTEN_SLOTS)
        tcp_listen[lid].active = 0;
}

/* Forward declaration — tcp_rx_process defined after net_ping */
static void tcp_rx_process(const uint8_t *ip_hdr, const uint8_t *tcp_seg, int tcp_total);

/* ================================================================== */
/* Byte-order helpers (x86 little-endian → network big-endian)        */
/* ================================================================== */
static uint16_t bswap16(uint16_t x) { return (uint16_t)((x >> 8) | (x << 8)); }
static uint32_t bswap32(uint32_t x) {
    return ((x & 0xFF) << 24) | (((x >> 8) & 0xFF) << 16)
         | (((x >> 16) & 0xFF) << 8) | ((x >> 24) & 0xFF);
}
#define htons(x)  bswap16((uint16_t)(x))
#define htonl(x)  bswap32((uint32_t)(x))
#define ntohs(x)  bswap16((uint16_t)(x))

/* ================================================================== */
/* Checksum IPv4 / ICMP (16-bit one's complement)                     */
/* ================================================================== */
static uint16_t inet_cksum(const uint8_t *data, int len) {
    uint32_t sum = 0;
    while (len > 1) { sum += ((uint32_t)data[0] << 8) | data[1]; data += 2; len -= 2; }
    if (len)         sum += (uint32_t)data[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

/* ================================================================== */
/* Helpers memori / cetak                                              */
/* ================================================================== */
static void mc(uint8_t *d, const uint8_t *s, int n) { while (n--) *d++ = *s++; }
static int  meq(const uint8_t *a, const uint8_t *b, int n) {
    while (n--) if (*a++ != *b++) return 0; return 1;
}

static void print_mac(const uint8_t m[6]) {
    const char *h = "0123456789abcdef";
    char buf[3]; buf[2] = 0;
    int i;
    for (i = 0; i < 6; i++) {
        buf[0] = h[m[i] >> 4]; buf[1] = h[m[i] & 0xF];
        print(buf);
        if (i < 5) print(":");
    }
}
static void print_ip(const uint8_t ip[4]) {
    char buf[6]; int i;
    for (i = 0; i < 4; i++) {
        itoa(ip[i], buf); print(buf);
        if (i < 3) print(".");
    }
}

/* ================================================================== */
/* ARP cache                                                          */
/* ================================================================== */
static void arp_cache_put(const uint8_t ip[4], const uint8_t mac[6]) {
    int i;
    for (i = 0; i < ARP_CACHE; i++) {
        if (!arp_tbl[i].valid || meq(arp_tbl[i].ip, ip, 4)) {
            mc(arp_tbl[i].ip,  ip,  4);
            mc(arp_tbl[i].mac, mac, 6);
            arp_tbl[i].valid = 1;
            return;
        }
    }
    /* Cache penuh: ganti slot 0 */
    mc(arp_tbl[0].ip,  ip,  4);
    mc(arp_tbl[0].mac, mac, 6);
}
static int arp_cache_get(const uint8_t ip[4], uint8_t mac[6]) {
    int i;
    for (i = 0; i < ARP_CACHE; i++)
        if (arp_tbl[i].valid && meq(arp_tbl[i].ip, ip, 4)) {
            mc(mac, arp_tbl[i].mac, 6); return 1;
        }
    return 0;
}

/* ================================================================== */
/* Bangun & kirim frame Ethernet                                      */
/* ================================================================== */

/* Isi Ethernet header di pkt_buf, return pointer ke payload setelah header */
static uint8_t *eth_hdr(const uint8_t dst[6], uint16_t ethertype) {
    mc(pkt_buf, dst, 6);
    mc(pkt_buf + 6, my_mac, 6);
    pkt_buf[12] = (uint8_t)(ethertype >> 8);
    pkt_buf[13] = (uint8_t)(ethertype & 0xFF);
    return pkt_buf + 14;
}

/* --- ARP request (broadcast): "siapa yang punya ip?" --- */
static void arp_send_request(const uint8_t target_ip[4]) {
    uint8_t *p = eth_hdr(BCAST_MAC, 0x0806);  /* ethertype ARP */
    /* ARP header */
    p[0]=0x00; p[1]=0x01;  /* htype=Ethernet */
    p[2]=0x08; p[3]=0x00;  /* ptype=IPv4 */
    p[4]=6;    p[5]=4;     /* hlen, plen */
    p[6]=0x00; p[7]=0x01;  /* oper=request */
    mc(p+8,  my_mac,    6);
    mc(p+14, MY_IP,     4);
    mc(p+18, BCAST_MAC, 6);  /* target MAC tidak diketahui */
    mc(p+24, target_ip, 4);
    rtl8139_send(pkt_buf, 14 + 28);
}

/* --- ARP reply: jawab ARP request untuk IP kita --- */
static void arp_send_reply(const uint8_t req_mac[6], const uint8_t req_ip[4]) {
    uint8_t *p = eth_hdr(req_mac, 0x0806);
    p[0]=0x00; p[1]=0x01;
    p[2]=0x08; p[3]=0x00;
    p[4]=6;    p[5]=4;
    p[6]=0x00; p[7]=0x02;  /* oper=reply */
    mc(p+8,  my_mac,  6);
    mc(p+14, MY_IP,   4);
    mc(p+18, req_mac, 6);
    mc(p+24, req_ip,  4);
    rtl8139_send(pkt_buf, 14 + 28);
}

/* --- ICMP echo request (ping) --- */
static void icmp_echo_send(const uint8_t dst_ip[4], const uint8_t dst_mac[6],
                           uint16_t seq) {
    uint8_t *p = eth_hdr(dst_mac, 0x0800);  /* ethertype IPv4 */

    /* IPv4 header (20 byte) */
    uint8_t *ip_hdr = p;
    /* total = 20 (IP) + 8 (ICMP hdr) + 56 (payload) = 84 byte
     * Tulis langsung tanpa htons — kita extract bytes secara big-endian sendiri */
    p[0]  = 0x45;                       /* ver=4, IHL=5 (20B) */
    p[1]  = 0x00;                       /* DSCP/ECN */
    p[2]  = (uint8_t)(84 >> 8);         /* total length MSB = 0x00 */
    p[3]  = (uint8_t)(84 & 0xFF);       /* total length LSB = 0x54 */
    p[4]  = (uint8_t)(ip_id_counter >> 8);
    p[5]  = (uint8_t)(ip_id_counter & 0xFF);
    ip_id_counter++;
    p[6]  = 0; p[7] = 0;               /* flags + frag offset */
    p[8]  = 64;                         /* TTL */
    p[9]  = 1;                          /* protocol ICMP */
    p[10] = 0; p[11] = 0;              /* checksum (isi nanti) */
    mc(p+12, MY_IP,   4);
    mc(p+16, dst_ip,  4);
    uint16_t ip_ck = inet_cksum(ip_hdr, 20);
    p[10] = (uint8_t)(ip_ck >> 8);
    p[11] = (uint8_t)(ip_ck & 0xFF);
    p += 20;

    /* ICMP header (8 byte) + 56 byte payload = 64 byte ICMP */
    uint8_t *icmp = p;
    p[0] = 8;   /* type: echo request */
    p[1] = 0;   /* code */
    p[2] = 0; p[3] = 0;                /* checksum (isi nanti) */
    p[4] = (uint8_t)(ICMP_ID >> 8);
    p[5] = (uint8_t)(ICMP_ID & 0xFF);
    p[6] = (uint8_t)(seq >> 8);
    p[7] = (uint8_t)(seq & 0xFF);
    /* 56 byte payload: 0x00, 0x01, ..., 0x37 */
    int i;
    for (i = 0; i < 56; i++) p[8 + i] = (uint8_t)i;
    uint16_t ic_ck = inet_cksum(icmp, 64);
    p[2] = (uint8_t)(ic_ck >> 8);
    p[3] = (uint8_t)(ic_ck & 0xFF);
    p += 64;

    rtl8139_send(pkt_buf, (uint16_t)(p - pkt_buf));
}

/* ================================================================== */
/* Proses satu paket yang diterima.                                    */
/* - ARP request→balas, ARP reply→cache.                              */
/* - ICMP echo reply dari expect_src → return 1 + *rtt.              */
/* ================================================================== */
static int net_process(const uint8_t *pkt, uint16_t len,
                       const uint8_t *expect_src, uint16_t *rtt_out,
                       uint32_t send_tick) {
    if (len < 14) return 0;
    uint16_t etype = ((uint16_t)pkt[12] << 8) | pkt[13];

    /* ---- ARP ---- */
    if (etype == 0x0806 && len >= 42) {
        uint16_t oper   = ((uint16_t)pkt[20] << 8) | pkt[21];
        const uint8_t *sha = pkt + 22;  /* sender hardware addr */
        const uint8_t *spa = pkt + 28;  /* sender protocol addr */
        if (oper == 2) {
            /* ARP reply: simpan ke cache */
            arp_cache_put(spa, sha);
        } else if (oper == 1) {
            /* ARP request: balas jika target adalah kita */
            const uint8_t *tpa = pkt + 38;
            if (meq(tpa, MY_IP, 4))
                arp_send_reply(sha, spa);
        }
        return 0;
    }

    /* ---- IPv4 ---- */
    if (etype == 0x0800 && len >= 34) {
        const uint8_t *ip  = pkt + 14;
        uint8_t ihl        = (uint8_t)((ip[0] & 0xF) * 4);
        uint8_t proto      = ip[9];
        const uint8_t *pay = ip + ihl;
        /* Gunakan IP total-length, bukan panjang frame Ethernet.
         * Frame Ethernet di-pad ke min 60 byte → padding masuk sebagai
         * "payload" palsu jika pakai (len-14-ihl). */
        uint16_t ip_total  = (uint16_t)(((uint16_t)ip[2] << 8) | ip[3]);
        int pay_len        = (int)ip_total - (int)ihl;
        if (pay_len < 0) pay_len = 0;

        /* Opportunistically cache pengirim IP→MAC */
        arp_cache_put(ip + 12, pkt + 6);

        /* ---- ICMP ---- */
        if (proto == 1 && pay_len >= 8) {
            uint8_t type = pay[0];
            /* Terima ICMP echo reply (type=0) dari IP tujuan.
             * Tidak cek ID/seq: QEMU SLIRP kadang memodifikasinya. */
            if (type == 0 && meq(ip + 12, expect_src, 4)) {
                if (rtt_out) *rtt_out = (uint16_t)(get_ticks() - send_tick);
                return 1;
            }
        }

        /* ---- TCP ---- */
        if (proto == 6 && pay_len >= 20)
            tcp_rx_process(ip, pay, pay_len);
    }
    return 0;
}

/* ================================================================== */
/* ARP resolution: resolve IP → MAC (timeout 2 detik)                */
/* ================================================================== */
static int arp_resolve(const uint8_t ip[4], uint8_t mac[6]) {
    if (arp_cache_get(ip, mac)) return 1;
    arp_send_request(ip);
    uint32_t deadline = get_ticks() + 2000;
    uint8_t  rbuf[1514];
    uint16_t rlen;
    while (get_ticks() < deadline) {
        if (rtl8139_recv(rbuf, &rlen) == 0)
            net_process(rbuf, rlen, 0, 0, 0);
        if (arp_cache_get(ip, mac)) return 1;
    }
    return 0;
}

/* ================================================================== */
/* Public API                                                         */
/* ================================================================== */
void net_init(void) {
    rtl8139_init();
    if (!rtl8139_present()) return;
    rtl8139_get_mac(my_mac);
    ip6_make_link_local();
}

int net_present(void) { return rtl8139_present(); }

void net_ifconfig(void) {
    if (!rtl8139_present()) {
        print("net: RTL8139 tidak ditemukan\n");
        return;
    }
    print("eth0  MAC : "); print_mac(my_mac);  print("\n");
    print("      IP  : "); print_ip(MY_IP);    print("/24\n");
    print("      GW  : "); print_ip(GW_IP);    print("\n");
}

int net_ping(const uint8_t dst_ip[4], int count) {
    if (!rtl8139_present()) {
        print("net: RTL8139 tidak ditemukan\n");
        return -1;
    }

    /* Tentukan next-hop: jika satu subnet (10.0.2.x) → langsung, jika tidak → gateway */
    uint8_t next_hop[4];
    if (dst_ip[0]==MY_IP[0] && dst_ip[1]==MY_IP[1] && dst_ip[2]==MY_IP[2])
        mc(next_hop, dst_ip, 4);
    else
        mc(next_hop, GW_IP, 4);

    /* Resolusi ARP */
    uint8_t dst_mac[6];
    print("PING "); print_ip(dst_ip); print(" via ");
    print_ip(next_hop); print("\n");
    print("ARP  resolving... ");
    if (!arp_resolve(next_hop, dst_mac)) {
        print("timeout\n");
        return -1;
    }
    print_mac(dst_mac); print("\n");

    /* Kirim 'count' ping */
    char buf[16];
    static uint8_t rbuf[1514];  /* static: hemat stack (tidak rekursif) */
    int i;
    for (i = 0; i < count; i++) {
        uint16_t seq       = (uint16_t)(i + 1);
        uint32_t send_tick = get_ticks();
        icmp_echo_send(dst_ip, dst_mac, seq);

        /* Tunggu ICMP echo reply maks 5 detik, hitung semua paket diterima */
        uint16_t rlen;
        uint16_t rtt     = 0;
        int      got     = 0;
        int      pkt_cnt = 0;
        uint32_t dead    = send_tick + 5000;
        while (get_ticks() < dead) {
            if (rtl8139_recv(rbuf, &rlen) == 0) {
                pkt_cnt++;
                if (net_process(rbuf, rlen, dst_ip, &rtt, send_tick)) { got = 1; break; }
            }
        }

        print("  seq="); itoa(seq, buf); print(buf); print("  ");
        if (got) {
            print("reply from "); print_ip(dst_ip);
            print("  rtt="); itoa(rtt, buf); print(buf); print(" ms\n");
        } else {
            print("timeout");
            if (pkt_cnt > 0) {
                /* Ada paket masuk tapi bukan ICMP reply yang dicari */
                print(" (recv "); itoa((uint32_t)pkt_cnt, buf); print(buf); print(" pkts)");
            }
            print("\n");
        }
    }
    return 0;
}

/* ================================================================== */
/* TCP/UDP implementation                                              */
/* ================================================================== */

/* ---- transport checksum (pseudo-header) ---- */
static uint16_t transport_cksum(const uint8_t src_ip[4], const uint8_t dst_ip[4],
                                 uint8_t proto, const uint8_t *seg, uint16_t seg_len) {
    uint8_t ph[12];
    int i;
    mc(ph,   src_ip, 4);
    mc(ph+4, dst_ip, 4);
    ph[8]  = 0; ph[9] = proto;
    ph[10] = (uint8_t)(seg_len >> 8);
    ph[11] = (uint8_t)(seg_len & 0xFF);
    uint32_t sum = 0;
    for (i = 0; i < 12; i += 2) sum += ((uint32_t)ph[i] << 8) | ph[i+1];
    const uint8_t *p = seg;
    int len = (int)seg_len;
    while (len > 1) { sum += ((uint32_t)p[0] << 8) | p[1]; p += 2; len -= 2; }
    if (len) sum += (uint32_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

/* ---- send one IP+TCP segment ---- */
static void tcp_tx(TcpConn *c, uint8_t flags, uint32_t seq, uint32_t ack,
                   const uint8_t *data, uint16_t dlen) {
    uint16_t tcp_tlen = (uint16_t)(20 + dlen);
    uint16_t ip_tlen  = (uint16_t)(20 + tcp_tlen);
    int i;

    uint8_t *p = eth_hdr(c->dst_mac, 0x0800);

    /* IP header */
    p[0]  = 0x45; p[1] = 0x00;
    p[2]  = (uint8_t)(ip_tlen >> 8);
    p[3]  = (uint8_t)(ip_tlen & 0xFF);
    p[4]  = (uint8_t)(ip_id_counter >> 8);
    p[5]  = (uint8_t)(ip_id_counter & 0xFF);
    ip_id_counter++;
    p[6]  = 0; p[7] = 0;
    p[8]  = 64; p[9] = 6;  /* TTL, IPPROTO_TCP */
    p[10] = 0; p[11] = 0;
    mc(p+12, MY_IP,     4);
    mc(p+16, c->dst_ip, 4);
    uint16_t ick = inet_cksum(p, 20);
    p[10] = (uint8_t)(ick >> 8); p[11] = (uint8_t)(ick & 0xFF);

    /* TCP header (20 bytes, no options) */
    uint8_t *t = p + 20;
    t[0]  = (uint8_t)(c->src_port >> 8);
    t[1]  = (uint8_t)(c->src_port & 0xFF);
    t[2]  = (uint8_t)(c->dst_port >> 8);
    t[3]  = (uint8_t)(c->dst_port & 0xFF);
    t[4]  = (uint8_t)(seq >> 24); t[5] = (uint8_t)(seq >> 16);
    t[6]  = (uint8_t)(seq >> 8);  t[7] = (uint8_t)(seq & 0xFF);
    t[8]  = (uint8_t)(ack >> 24); t[9] = (uint8_t)(ack >> 16);
    t[10] = (uint8_t)(ack >> 8);  t[11] = (uint8_t)(ack & 0xFF);
    t[12] = 0x50;  /* data offset = 5 words = 20 bytes */
    t[13] = flags;
    t[14] = 0xFF; t[15] = 0xFF;  /* window = 65535 */
    t[16] = 0; t[17] = 0;        /* checksum placeholder */
    t[18] = 0; t[19] = 0;        /* urgent */
    for (i = 0; i < (int)dlen; i++) t[20 + i] = data[i];
    uint16_t tck = transport_cksum(MY_IP, c->dst_ip, 6, t, tcp_tlen);
    t[16] = (uint8_t)(tck >> 8); t[17] = (uint8_t)(tck & 0xFF);

    rtl8139_send(pkt_buf, (uint16_t)(14 + 20 + tcp_tlen));
}

/* ---- push received data into connection rx ring buffer ---- */
static void tcp_rx_push(TcpConn *c, const uint8_t *data, uint16_t len) {
    uint16_t i;
    for (i = 0; i < len; i++) {
        uint16_t next = (uint16_t)((c->rx_head + 1) % TCP_RX_SIZE);
        if (next == c->rx_tail) break;  /* full: drop */
        c->rx[c->rx_head] = data[i];
        c->rx_head = next;
    }
}

/* ---- process one incoming TCP segment (called from net_process) ---- */
static void tcp_rx_process(const uint8_t *ip_hdr, const uint8_t *tcp_seg, int tcp_total) {
    const uint8_t *src_ip = ip_hdr + 12;
    uint16_t rsp  = (uint16_t)(((uint16_t)tcp_seg[0] << 8) | tcp_seg[1]);  /* remote src port */
    uint16_t rdp  = (uint16_t)(((uint16_t)tcp_seg[2] << 8) | tcp_seg[3]);  /* remote dst port (= our src) */
    uint32_t sseq = ((uint32_t)tcp_seg[4]<<24)|((uint32_t)tcp_seg[5]<<16)|((uint32_t)tcp_seg[6]<<8)|tcp_seg[7];
    uint32_t sack = ((uint32_t)tcp_seg[8]<<24)|((uint32_t)tcp_seg[9]<<16)|((uint32_t)tcp_seg[10]<<8)|tcp_seg[11];
    uint8_t  doff = (uint8_t)((tcp_seg[12] >> 4) * 4);  /* TCP header length */
    uint8_t  flags= tcp_seg[13];
    int      dlen = tcp_total - (int)doff;
    if (dlen < 0) dlen = 0;
    const uint8_t *data = tcp_seg + doff;
    int i;

    for (i = 0; i < TCP_MAX_CONN; i++) {
        TcpConn *c = &tcp_conns[i];
        if (!c->used) continue;
        if (c->src_port != rdp) continue;
        if (c->dst_port != rsp) continue;
        if (!meq(c->dst_ip, src_ip, 4)) continue;

        /* RST: abort */
        if (flags & TCP_RST) { c->rst_recv = 1; c->state = TCP_CLOSED; c->used = 0; return; }

        if (c->state == TCP_SYN_SENT) {
            /* Expect SYN+ACK */
            if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
                c->rcv_nxt = sseq + 1;
                c->snd_nxt = sack;
                c->state   = TCP_ESTABLISHED;
                tcp_tx(c, TCP_ACK, c->snd_nxt, c->rcv_nxt, 0, 0);
            }
        } else if (c->state == TCP_ESTABLISHED || c->state == TCP_FIN_WAIT1
                                                || c->state == TCP_FIN_WAIT2) {
            /* F-X1: ACK clears retransmit buffer jika semua data ter-ack */
            if (flags & TCP_ACK) {
                if (c->tx_len > 0 && sack >= c->tx_seq + c->tx_len)
                    c->tx_len = 0;
                if (c->state == TCP_FIN_WAIT1 && sack == c->snd_nxt)
                    c->state = TCP_FIN_WAIT2;
                else
                    c->snd_nxt = sack;
            }
            /* Accept incoming data */
            if (dlen > 0 && c->state == TCP_ESTABLISHED) {
                c->last_rx_tick = get_ticks();         /* F-X3: reset keepalive idle */
                c->ka_probes    = 0;
                c->ka_next_tick = get_ticks() + TCP_KEEPALIVE_IDLE;
                if (sseq == c->rcv_nxt) {
                    /* In-order: terima langsung */
                    tcp_rx_push(c, data, (uint16_t)dlen);
                    c->rcv_nxt += (uint32_t)dlen;
                    /* F-X2: cek apakah ada OOO segment yang kini bisa digabung */
                    if (c->ooo_len > 0 && c->ooo_seq == c->rcv_nxt) {
                        tcp_rx_push(c, c->ooo_buf, c->ooo_len);
                        c->rcv_nxt += c->ooo_len;
                        c->ooo_len  = 0;
                    }
                    tcp_tx(c, TCP_ACK, c->snd_nxt, c->rcv_nxt, 0, 0);
                } else if (sseq > c->rcv_nxt && dlen <= (int)TCP_TX_SIZE) {
                    /* F-X2: out-of-order — simpan satu slot, kirim duplicate ACK */
                    if (c->ooo_len == 0) {
                        int oi; for (oi = 0; oi < dlen; oi++) c->ooo_buf[oi] = data[oi];
                        c->ooo_len = (uint16_t)dlen;
                        c->ooo_seq = sseq;
                    }
                    /* duplicate ACK */
                    tcp_tx(c, TCP_ACK, c->snd_nxt, c->rcv_nxt, 0, 0);
                } else {
                    /* dup / retrans data sudah dimiliki — ACK saja */
                    tcp_tx(c, TCP_ACK, c->snd_nxt, c->rcv_nxt, 0, 0);
                }
            }
            /* Remote FIN */
            if (flags & TCP_FIN) {
                c->rcv_nxt++;
                c->fin_recv = 1;
                tcp_tx(c, TCP_ACK, c->snd_nxt, c->rcv_nxt, 0, 0);
                if (c->state == TCP_ESTABLISHED) c->state = TCP_CLOSE_WAIT;
                else                             c->state = TCP_TIME_WAIT;
            }
        } else if (c->state == TCP_LAST_ACK) {
            if (flags & TCP_ACK) { c->used = 0; c->state = TCP_CLOSED; }
        }
        break;
    }

    /* ---- Fondasi AM: handle inbound SYN ke port yang di-listen ---- */
    if ((flags & (TCP_SYN | TCP_ACK)) == TCP_SYN) {
        /* Cari listen slot yang cocok */
        int li;
        for (li = 0; li < TCP_LISTEN_SLOTS; li++) {
            if (!tcp_listen[li].active) continue;
            if (tcp_listen[li].port != rdp) continue;
            /* rdp = our port (dst of incoming packet) */
            if (tcp_listen[li].aq_count >= 2) break; /* accept queue penuh */

            /* Alokasikan TcpConn baru */
            int ci;
            for (ci = 0; ci < TCP_MAX_CONN; ci++) {
                if (!tcp_conns[ci].used) break;
            }
            if (ci >= TCP_MAX_CONN) break;

            TcpConn *nc = &tcp_conns[ci];
            int ki;
            for (ki = 0; ki < 4; ki++) nc->dst_ip[ki] = src_ip[ki];
            for (ki = 0; ki < 6; ki++) nc->dst_mac[ki] = ip_hdr[-14 + ki + 6]; /* src MAC dari Ethernet */
            nc->src_port   = rdp;          /* our port = server port */
            nc->dst_port   = rsp;          /* remote client port */
            nc->rcv_nxt    = sseq + 1;
            nc->snd_nxt    = 0xAC000001;   /* ISN arbitrary */
            nc->state      = TCP_ESTABLISHED;
            nc->used       = 1;
            nc->fin_recv   = 0;
            nc->rst_recv   = 0;
            nc->tx_len     = 0;
            nc->rx_head    = 0;
            nc->rx_tail    = 0;
            nc->ooo_len    = 0;
            nc->ka_probes  = 0;
            nc->last_rx_tick = get_ticks();
            nc->ka_next_tick = get_ticks() + TCP_KEEPALIVE_IDLE;

            /* Kirim SYN+ACK */
            tcp_tx(nc, TCP_SYN | TCP_ACK, nc->snd_nxt - 1, nc->rcv_nxt, 0, 0);
            nc->snd_nxt++;  /* SYN mengkonsumsi 1 seq number */

            /* Masukkan ke accept queue */
            tcp_listen[li].accept_q[tcp_listen[li].aq_tail] = (int8_t)ci;
            tcp_listen[li].aq_tail  = (tcp_listen[li].aq_tail + 1) % 2;
            tcp_listen[li].aq_count++;
            break;
        }
    }
}

/* ---- next-hop: same subnet → direct, else gateway ---- */
static void next_hop_ip(const uint8_t dst[4], uint8_t out[4]) {
    if (dst[0]==MY_IP[0] && dst[1]==MY_IP[1] && dst[2]==MY_IP[2])
        mc(out, dst, 4);
    else
        mc(out, GW_IP, 4);
}

/* ================================================================== */
/* F-X4: DNS resolver — kirim query A record UDP ke 8.8.8.8:53       */
/* ================================================================== */
#define DNS_PORT      53
#define DNS_SRC_PORT  5353  /* source port lokal untuk DNS query */
#define DNS_MAX_PKT   512

/* DNS server publik (Google) */
static const uint8_t DNS_SERVER[4] = {8, 8, 8, 8};

/* Bangun DNS query untuk hostname → A record.
 * buf harus ukuran minimal DNS_MAX_PKT.
 * Return panjang query. */
static int dns_build_query(uint8_t *buf, const char *hostname, uint16_t txid) {
    int i, j;
    /* Header: TXID, flags(standard query), 1 question */
    buf[0]  = (uint8_t)(txid >> 8);
    buf[1]  = (uint8_t)(txid & 0xFF);
    buf[2]  = 0x01; buf[3] = 0x00;  /* recursion desired */
    buf[4]  = 0x00; buf[5] = 0x01;  /* QDCOUNT = 1 */
    buf[6]  = 0x00; buf[7] = 0x00;  /* ANCOUNT = 0 */
    buf[8]  = 0x00; buf[9] = 0x00;  /* NSCOUNT = 0 */
    buf[10] = 0x00; buf[11] = 0x00; /* ARCOUNT = 0 */

    /* QNAME: setiap label diawali panjangnya */
    int pos = 12;
    const char *p = hostname;
    while (*p) {
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        int llen = (int)(dot - p);
        buf[pos++] = (uint8_t)llen;
        for (j = 0; j < llen; j++) buf[pos++] = (uint8_t)p[j];
        p = dot;
        if (*p == '.') p++;
    }
    buf[pos++] = 0;          /* terminasi QNAME */
    buf[pos++] = 0x00; buf[pos++] = 0x01;  /* QTYPE = A (1) */
    buf[pos++] = 0x00; buf[pos++] = 0x01;  /* QCLASS = IN (1) */
    (void)i;
    return pos;
}

/* Parse DNS response, cari A record pertama, isi out_ip[4].
 * Return 1 sukses, 0 gagal. */
static int dns_parse_response(const uint8_t *buf, int len, uint16_t txid,
                              uint8_t out_ip[4]) {
    if (len < 12) return 0;
    uint16_t rid = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    if (rid != txid) return 0;
    if (!(buf[2] & 0x80)) return 0;   /* bukan response */
    uint16_t ancount = (uint16_t)(((uint16_t)buf[6] << 8) | buf[7]);
    if (ancount == 0) return 0;

    /* Skip question section */
    int pos = 12;
    /* Skip QNAME */
    while (pos < len) {
        uint8_t llen = buf[pos++];
        if (llen == 0) break;
        if ((llen & 0xC0) == 0xC0) { pos++; break; }  /* pointer */
        pos += llen;
    }
    pos += 4;  /* skip QTYPE + QCLASS */

    /* Parse answer records */
    int a;
    for (a = 0; a < (int)ancount && pos < len; a++) {
        /* Skip NAME (may be pointer) */
        if (pos < len && (buf[pos] & 0xC0) == 0xC0) pos += 2;
        else {
            while (pos < len) {
                uint8_t llen = buf[pos++];
                if (llen == 0) break;
                pos += llen;
            }
        }
        if (pos + 10 > len) break;
        uint16_t rtype  = (uint16_t)(((uint16_t)buf[pos] << 8) | buf[pos+1]);
        uint16_t rdlen  = (uint16_t)(((uint16_t)buf[pos+8] << 8) | buf[pos+9]);
        pos += 10;  /* skip TYPE(2)+CLASS(2)+TTL(4)+RDLENGTH(2) */
        if (rtype == 1 && rdlen == 4 && pos + 4 <= len) {
            /* A record! */
            out_ip[0] = buf[pos];
            out_ip[1] = buf[pos+1];
            out_ip[2] = buf[pos+2];
            out_ip[3] = buf[pos+3];
            return 1;
        }
        pos += rdlen;
    }
    return 0;
}

/* dns_resolve(hostname, out_ip): kirim UDP DNS query, tunggu reply.
 * Return 1 sukses + out_ip diisi, 0 gagal/timeout. */
int dns_resolve(const char *hostname, uint8_t out_ip[4]) {
    if (!rtl8139_present()) return 0;
    uint8_t qbuf[DNS_MAX_PKT];
    static uint16_t dns_txid = 0x4400;
    uint16_t txid = ++dns_txid;
    int qlen = dns_build_query(qbuf, hostname, txid);

    /* Kirim 3 kali (timeout 1s per attempt) */
    int attempt;
    for (attempt = 0; attempt < 3; attempt++) {
        net_udp_send(DNS_SERVER, DNS_PORT, DNS_SRC_PORT, qbuf, (uint16_t)qlen);

        uint32_t deadline = get_ticks() + 1000;
        while (get_ticks() < deadline) {
            /* Poll NIC untuk paket masuk */
            uint8_t pkt[1514]; uint16_t plen;
            if (rtl8139_recv(pkt, &plen) == 0 && plen > 42) {
                /* Cek: UDP, dari port 53, ke DNS_SRC_PORT */
                uint8_t *ip  = pkt + 14;
                if (ip[9] != 17) continue;   /* bukan UDP */
                uint8_t *udp = ip + (ip[0] & 0xF) * 4;
                uint16_t sp = (uint16_t)(((uint16_t)udp[0] << 8) | udp[1]);
                uint16_t dp = (uint16_t)(((uint16_t)udp[2] << 8) | udp[3]);
                if (sp != DNS_PORT || dp != DNS_SRC_PORT) {
                    /* Bukan DNS reply: proses sebagai paket biasa */
                    net_process(pkt, plen, 0, 0, 0);
                    continue;
                }
                uint8_t *dns_payload = udp + 8;
                int dns_len = (int)plen - 14 - (int)((ip[0] & 0xF) * 4) - 8;
                if (dns_parse_response(dns_payload, dns_len, txid, out_ip))
                    return 1;
            }
        }
    }
    return 0;
}

/* ================================================================== */
/* Public API — UDP                                                    */
/* ================================================================== */
int net_udp_send(const uint8_t dst_ip[4], uint16_t dst_port,
                 uint16_t src_port, const void *data, uint16_t dlen) {
    if (!rtl8139_present()) return -1;
    uint8_t nh[4]; next_hop_ip(dst_ip, nh);
    uint8_t dst_mac[6];
    if (!arp_resolve(nh, dst_mac)) return -1;

    uint16_t udp_len = (uint16_t)(8 + dlen);
    uint16_t ip_tlen = (uint16_t)(20 + udp_len);
    int i;

    uint8_t *p = eth_hdr(dst_mac, 0x0800);
    p[0]  = 0x45; p[1] = 0x00;
    p[2]  = (uint8_t)(ip_tlen >> 8);
    p[3]  = (uint8_t)(ip_tlen & 0xFF);
    p[4]  = (uint8_t)(ip_id_counter >> 8);
    p[5]  = (uint8_t)(ip_id_counter & 0xFF);
    ip_id_counter++;
    p[6]  = 0; p[7] = 0;
    p[8]  = 64; p[9] = 17;  /* TTL, IPPROTO_UDP */
    p[10] = 0; p[11] = 0;
    mc(p+12, MY_IP,  4);
    mc(p+16, dst_ip, 4);
    uint16_t ick = inet_cksum(p, 20);
    p[10] = (uint8_t)(ick >> 8); p[11] = (uint8_t)(ick & 0xFF);

    uint8_t *u = p + 20;
    u[0] = (uint8_t)(src_port >> 8); u[1] = (uint8_t)(src_port & 0xFF);
    u[2] = (uint8_t)(dst_port >> 8); u[3] = (uint8_t)(dst_port & 0xFF);
    u[4] = (uint8_t)(udp_len >> 8);  u[5] = (uint8_t)(udp_len & 0xFF);
    u[6] = 0; u[7] = 0;  /* checksum = 0 (optional for UDP/IPv4) */
    for (i = 0; i < (int)dlen; i++) u[8 + i] = ((const uint8_t*)data)[i];

    rtl8139_send(pkt_buf, (uint16_t)(14 + 20 + udp_len));
    return 0;
}

/* ================================================================== */
/* Public API — TCP                                                    */
/* ================================================================== */
static void net_poll_once(void) {
    uint8_t buf[1514]; uint16_t len;
    if (rtl8139_recv(buf, &len) == 0)
        net_process(buf, len, 0, 0, 0);
}

int net_tcp_connect(const uint8_t dst_ip[4], uint16_t dst_port) {
    if (!rtl8139_present()) return -1;
    int id = -1, i;
    for (i = 0; i < TCP_MAX_CONN; i++) if (!tcp_conns[i].used) { id = i; break; }
    if (id < 0) return -1;

    TcpConn *c = &tcp_conns[id];
    uint8_t nh[4]; next_hop_ip(dst_ip, nh);
    if (!arp_resolve(nh, c->dst_mac)) return -1;

    mc(c->dst_ip, dst_ip, 4);
    c->dst_port      = dst_port;
    c->src_port      = tcp_port_ctr++;
    if (tcp_port_ctr < 49152) tcp_port_ctr = 49152;
    c->snd_nxt       = 0x12340000u + (uint32_t)(c->src_port * 131u);
    c->rcv_nxt       = 0;
    c->rx_head       = c->rx_tail = 0;
    c->fin_recv      = 0;
    c->rst_recv      = 0;
    c->tx_len        = 0;
    c->tx_seq        = 0;
    c->retrans_tick  = 0;
    c->retrans_count = 0;
    c->ooo_len       = 0;
    c->ooo_seq       = 0;
    c->last_rx_tick  = get_ticks();
    c->ka_probes     = 0;
    c->ka_next_tick  = get_ticks() + TCP_KEEPALIVE_IDLE;
    c->state         = TCP_SYN_SENT;
    c->used          = 1;

    /* Send SYN */
    tcp_tx(c, TCP_SYN, c->snd_nxt, 0, 0, 0);
    c->snd_nxt++;

    /* Wait up to 5s for SYN-ACK */
    uint32_t deadline = get_ticks() + 5000;
    while (get_ticks() < deadline) {
        net_poll_once();
        if (c->state == TCP_ESTABLISHED) return id;
        if (!c->used) break;  /* RST received */
    }
    int was_rst = c->rst_recv;
    c->used = 0; c->state = TCP_CLOSED;
    return was_rst ? -2 : -1;  /* -2 = refused/RST, -1 = timeout */
}

int net_tcp_send(int id, const void *data, uint16_t len) {
    if (id < 0 || id >= TCP_MAX_CONN) return -1;
    TcpConn *c = &tcp_conns[id];
    if (!c->used || c->state != TCP_ESTABLISHED) return -1;
    uint16_t chunk = len > TCP_TX_SIZE ? (uint16_t)TCP_TX_SIZE : len;
    tcp_tx(c, TCP_PSH | TCP_ACK, c->snd_nxt, c->rcv_nxt, (const uint8_t*)data, chunk);
    /* F-X1: simpan ke tx_buf untuk retransmit jika ACK tidak datang */
    c->tx_seq = c->snd_nxt;
    c->tx_len = chunk;
    int j; for (j = 0; j < (int)chunk; j++) c->tx_buf[j] = ((const uint8_t*)data)[j];
    c->retrans_tick  = get_ticks();
    c->retrans_count = 0;
    c->snd_nxt += chunk;
    return (int)chunk;
}

/* Return bytes read, 0 = connection closed / timeout, -1 = error */
int net_tcp_recv(int id, void *buf, uint16_t maxlen) {
    if (id < 0 || id >= TCP_MAX_CONN) return -1;
    TcpConn *c = &tcp_conns[id];
    if (!c->used) return -1;

    uint32_t deadline = get_ticks() + 5000;
    while (get_ticks() < deadline) {
        net_poll_once();
        /* Return any buffered data */
        if (c->rx_head != c->rx_tail) {
            uint8_t *out = (uint8_t *)buf;
            uint16_t got = 0;
            while (c->rx_tail != c->rx_head && got < maxlen) {
                out[got++] = c->rx[c->rx_tail];
                c->rx_tail = (uint16_t)((c->rx_tail + 1) % TCP_RX_SIZE);
            }
            return (int)got;
        }
        /* Remote closed and no more data */
        if (c->fin_recv)              return 0;
        if (c->state == TCP_CLOSE_WAIT) return 0;
        if (c->state == TCP_CLOSED)     return 0;
        if (!c->used)                   return 0;
    }
    return 0;  /* timeout */
}

void net_tcp_close(int id) {
    if (id < 0 || id >= TCP_MAX_CONN) return;
    TcpConn *c = &tcp_conns[id];
    if (!c->used) return;
    if (c->state == TCP_ESTABLISHED || c->state == TCP_CLOSE_WAIT) {
        tcp_tx(c, TCP_FIN | TCP_ACK, c->snd_nxt, c->rcv_nxt, 0, 0);
        c->snd_nxt++;
        c->state = (c->state == TCP_ESTABLISHED) ? TCP_FIN_WAIT1 : TCP_LAST_ACK;
        uint32_t deadline = get_ticks() + 3000;
        while (get_ticks() < deadline && c->used) {
            net_poll_once();
            if (c->state == TCP_TIME_WAIT || c->state == TCP_CLOSED) break;
        }
    }
    c->used = 0; c->state = TCP_CLOSED;
}

int net_tcp_state(int id) {
    if (id < 0 || id >= TCP_MAX_CONN || !tcp_conns[id].used) return TCP_CLOSED;
    return tcp_conns[id].state;
}

void net_poll(void) { net_poll_once(); }

/* ================================================================== */
/* F-X1/X3: Periodic TCP timer — dipanggil dari timer IRQ setiap ~10ms */
/* Periksa retransmit timeout dan keepalive untuk semua koneksi aktif.  */
/* ================================================================== */
void net_tcp_tick(void) {
    if (!rtl8139_present()) return;
    uint32_t now = get_ticks();
    int i;
    for (i = 0; i < TCP_MAX_CONN; i++) {
        TcpConn *c = &tcp_conns[i];
        if (!c->used || c->state != TCP_ESTABLISHED) continue;

        /* F-X1: Retransmit — ada data belum di-ACK dan RTO terlewat */
        if (c->tx_len > 0 && (now - c->retrans_tick) >= TCP_RTO) {
            if (c->retrans_count >= TCP_MAX_RETRANS) {
                /* Gagal total: kirim RST, tutup koneksi */
                tcp_tx(c, TCP_RST | TCP_ACK, c->snd_nxt, c->rcv_nxt, 0, 0);
                c->used = 0;
                c->state = TCP_CLOSED;
                continue;
            }
            /* Retransmit paket terakhir */
            tcp_tx(c, TCP_PSH | TCP_ACK, c->tx_seq, c->rcv_nxt,
                   c->tx_buf, c->tx_len);
            c->retrans_tick = now;
            c->retrans_count++;
        }

        /* F-X3: Keepalive — kirim probe jika idle terlalu lama */
        if ((now - c->last_rx_tick) >= TCP_KEEPALIVE_IDLE && now >= c->ka_next_tick) {
            if (c->ka_probes >= TCP_KEEPALIVE_CNT) {
                /* Tidak ada respons: tutup koneksi */
                tcp_tx(c, TCP_RST | TCP_ACK, c->snd_nxt, c->rcv_nxt, 0, 0);
                c->used  = 0;
                c->state = TCP_CLOSED;
                continue;
            }
            /* Kirim keepalive probe: ACK dengan snd_nxt-1 (satu byte fiktif) */
            tcp_tx(c, TCP_ACK, c->snd_nxt - 1, c->rcv_nxt, 0, 0);
            c->ka_probes++;
            c->ka_next_tick = now + TCP_KEEPALIVE_INTVL;
        }
    }
}

/* ================================================================== */
/* F-Z: http_get(url) — HTTP/1.1 GET + chunked decode + redirect      */
/* url format: "http://hostname/path" atau "http://hostname"           */
/* Cetak status (warna), header (abu-abu), body (putih).               */
/* Return 0 sukses, -1 error.                                          */
/* ================================================================== */

/* Warna untuk header dan body HTTP */
#define HTTP_COLOR_HEADER  0x888888u  /* abu-abu */
#define HTTP_COLOR_BODY    0xFFFFFFu  /* putih   */
#define HTTP_COLOR_ERROR   0xFF4444u  /* merah   */
#define HTTP_COLOR_INFO    0x44FF44u  /* hijau   */
#define HTTP_COLOR_WARN    0xFFFF00u  /* kuning  */
#define HTTP_COLOR_BG      0x000000u  /* hitam   */

#define HTTP_WORK_BUF   8192
#define HTTP_MAX_REDIR  3

static uint8_t http_work[HTTP_WORK_BUF];
static char    http_decoded[HTTP_WORK_BUF];

/* Helper: hitung panjang string */
static int http_strlen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}

/* Helper: append string ke dst, return pointer setelah akhir */
static char *http_append(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    return dst;
}

/* Helper: parse URL "http://host/path" atau "http://host"
 * Return 1 sukses, 0 gagal. */
static int parse_http_url(const char *url,
                          char host_out[128], char path_out[256]) {
    const char *p = url;
    if (p[0]!='h'||p[1]!='t'||p[2]!='t'||p[3]!='p'||
        p[4]!=':'||p[5]!='/'||p[6]!='/') return 0;
    p += 7;
    int hi = 0;
    while (*p && *p != '/' && hi < 127) host_out[hi++] = *p++;
    host_out[hi] = '\0';
    if (hi == 0) return 0;
    if (*p == '\0') { path_out[0]='/'; path_out[1]='\0'; }
    else { int pi=0; while (*p && pi<255) path_out[pi++]=*p++; path_out[pi]='\0'; }
    return 1;
}

/* Helper: parse status code dari "HTTP/1.x NNN ..." */
static int http_parse_status(const char *hdr) {
    int i = 0;
    while (hdr[i] && hdr[i] != ' ') i++;
    while (hdr[i] == ' ') i++;
    int code = 0;
    while (hdr[i] >= '0' && hdr[i] <= '9') { code = code*10 + (hdr[i]-'0'); i++; }
    return code;
}

/* Helper: cari nilai header "Field:" dalam blok header hdr (hlen byte).
 * Return pointer ke nilai, tulis panjang ke *vlen. Return 0 jika tidak ada. */
static const char *http_find_field(const char *hdr, int hlen,
                                   const char *field, int *vlen) {
    int flen = http_strlen(field);
    int i;
    for (i = 0; i < hlen - flen - 2; i++) {
        if (hdr[i] == '\n') {
            int j = i + 1, k;
            int match = 1;
            for (k = 0; k < flen; k++) {
                char a = hdr[j+k], b = field[k];
                if (a>='A'&&a<='Z') a+=32;
                if (b>='A'&&b<='Z') b+=32;
                if (a != b) { match=0; break; }
            }
            if (match && hdr[j+flen] == ':') {
                int vi = j + flen + 1;
                while (vi < hlen && hdr[vi] == ' ') vi++;
                int ve = vi;
                while (ve < hlen && hdr[ve] != '\r' && hdr[ve] != '\n') ve++;
                *vlen = ve - vi;
                return hdr + vi;
            }
        }
    }
    return 0;
}

/* Helper: decode Transfer-Encoding: chunked ke http_decoded.
 * Return jumlah byte body setelah decode. */
static int http_decode_chunked(const char *src, int slen) {
    int si = 0, di = 0;
    while (si < slen && di < HTTP_WORK_BUF - 1) {
        /* Baca hex chunk size */
        int csz = 0;
        while (si < slen) {
            char c = src[si];
            if      (c>='0'&&c<='9') { csz=csz*16+(c-'0');    si++; }
            else if (c>='a'&&c<='f') { csz=csz*16+(c-'a'+10); si++; }
            else if (c>='A'&&c<='F') { csz=csz*16+(c-'A'+10); si++; }
            else break;
        }
        /* Skip hingga \n (chunk extension / \r\n) */
        while (si < slen && src[si] != '\n') si++;
        if (si < slen) si++;  /* skip \n */
        if (csz == 0) break;  /* chunk terakhir */
        /* Salin csz byte */
        int j;
        for (j = 0; j < csz && si < slen && di < HTTP_WORK_BUF-1; j++)
            http_decoded[di++] = src[si++];
        /* Skip \r\n setelah data chunk */
        while (si < slen && (src[si]=='\r'||src[si]=='\n')) si++;
    }
    http_decoded[di] = '\0';
    return di;
}

int http_get(const char *url) {
    static char cur_url[512];
    /* Salin url ke cur_url */
    { int i=0; while (url[i]&&i<511) { cur_url[i]=url[i]; i++; } cur_url[i]='\0'; }

    int redir;
    for (redir = 0; redir <= HTTP_MAX_REDIR; redir++) {
        char host[128], path[256];
        if (!parse_http_url(cur_url, host, path)) {
            set_color(HTTP_COLOR_ERROR, HTTP_COLOR_BG);
            print("curl: URL tidak valid. Gunakan: http://hostname/path\n");
            set_color(HTTP_COLOR_BODY, HTTP_COLOR_BG);
            return -1;
        }

        /* DNS resolve */
        set_color(HTTP_COLOR_INFO, HTTP_COLOR_BG);
        print("curl: resolving "); print(host); print("...\n");
        set_color(HTTP_COLOR_BODY, HTTP_COLOR_BG);
        uint8_t ip[4];
        if (!dns_resolve(host, ip)) {
            set_color(HTTP_COLOR_ERROR, HTTP_COLOR_BG);
            print("curl: gagal resolve '"); print(host); print("'\n");
            set_color(HTTP_COLOR_BODY, HTTP_COLOR_BG);
            return -1;
        }

        /* TCP connect */
        char nbuf[10];
        set_color(HTTP_COLOR_INFO, HTTP_COLOR_BG);
        print("curl: connecting ");
        itoa(ip[0],nbuf); print(nbuf); print(".");
        itoa(ip[1],nbuf); print(nbuf); print(".");
        itoa(ip[2],nbuf); print(nbuf); print(".");
        itoa(ip[3],nbuf); print(nbuf); print(":80...\n");
        set_color(HTTP_COLOR_BODY, HTTP_COLOR_BG);

        int conn = net_tcp_connect(ip, 80);
        if (conn < 0) {
            set_color(HTTP_COLOR_ERROR, HTTP_COLOR_BG);
            print(conn == -2 ? "curl: connection refused\n"
                             : "curl: connection timeout\n");
            set_color(HTTP_COLOR_BODY, HTTP_COLOR_BG);
            return -1;
        }

        /* Kirim HTTP/1.1 request */
        static char req[600];
        char *w = req;
        w = http_append(w, "GET "); w = http_append(w, path);
        w = http_append(w, " HTTP/1.1\r\nHost: "); w = http_append(w, host);
        w = http_append(w, "\r\nConnection: close\r\nUser-Agent: OriaOS/1.0\r\n\r\n");
        *w = '\0';
        net_tcp_send(conn, req, (uint16_t)http_strlen(req));

        /* ---- Fase 1: buffer header hingga \r\n\r\n ---- */
        static char hbuf[2048];
        int hlen = 0, hdr_done = 0;
        unsigned char hw[4] = {0,0,0,0};
        char tbuf[128]; int tn;
        /* sisa bytes body yang sudah masuk saat scan header */
        static char body_prefix[256]; int bpfx = 0;

        set_color(HTTP_COLOR_HEADER, HTTP_COLOR_BG);
        while (!hdr_done && (tn = net_tcp_recv(conn, (uint8_t*)tbuf, 127)) > 0) {
            int ti;
            for (ti = 0; ti < tn; ti++) {
                unsigned char ch = (unsigned char)tbuf[ti];
                if (!hdr_done) {
                    if (hlen < 2047) { hbuf[hlen++] = (char)ch; }
                    hw[0]=hw[1]; hw[1]=hw[2]; hw[2]=hw[3]; hw[3]=ch;
                    if (hw[0]=='\r' && hw[1]=='\n' &&
                        hw[2]=='\r' && hw[3]=='\n') {
                        hdr_done = 1;
                    }
                } else {
                    /* bytes body setelah \r\n\r\n dalam chunk yang sama */
                    if (bpfx < 255) body_prefix[bpfx++] = (char)ch;
                }
            }
        }
        hbuf[hlen] = '\0';

        /* Cetak header */
        { int pi; for (pi = 0; pi < hlen; pi++) {
            char s2[2]; s2[0]=hbuf[pi]; s2[1]='\0'; print(s2);
        }}

        /* ---- Parse status code ---- */
        int status = http_parse_status(hbuf);

        /* ---- Cek redirect (3xx) ---- */
        if (status >= 300 && status < 400) {
            int vlen = 0;
            const char *loc = http_find_field(hbuf, hlen, "Location", &vlen);
            net_tcp_close(conn);
            if (loc && vlen > 0) {
                /* Pastikan URL masuk ke cur_url */
                if (vlen < 511) {
                    int ci; for (ci=0;ci<vlen;ci++) cur_url[ci]=loc[ci];
                    cur_url[vlen] = '\0';
                }
                /* Hanya ikuti redirect http:// */
                if (loc[0]=='h' && loc[1]=='t' && loc[2]=='t' &&
                    loc[3]=='p' && loc[4]==':' && loc[5]=='/' && loc[6]=='/') {
                    set_color(HTTP_COLOR_WARN, HTTP_COLOR_BG);
                    print("\ncurl: redirect → "); print(cur_url); print("\n");
                    set_color(HTTP_COLOR_BODY, HTTP_COLOR_BG);
                    continue;  /* ikuti redirect */
                } else {
                    /* HTTPS atau protocol lain — tidak didukung */
                    set_color(HTTP_COLOR_WARN, HTTP_COLOR_BG);
                    print("\ncurl: redirect ke "); print(cur_url);
                    print(" (HTTPS tidak didukung)\n");
                    set_color(HTTP_COLOR_BODY, HTTP_COLOR_BG);
                    return -1;
                }
            }
            /* Redirect tanpa Location — tampilkan apa adanya */
            set_color(HTTP_COLOR_WARN, HTTP_COLOR_BG);
            { char sc[6]; itoa((uint32_t)status,sc);
              print("\ncurl: redirect "); print(sc); print(" tanpa Location\n"); }
            set_color(HTTP_COLOR_BODY, HTTP_COLOR_BG);
            return -1;
        }

        /* ---- Fase 2: stream body ---- */
        int body_bytes = 0;
        set_color(HTTP_COLOR_BODY, HTTP_COLOR_BG);

        /* Proses body_prefix (bytes yang ikut chunk header) */
        { int bi; for (bi = 0; bi < bpfx; bi++) {
            unsigned char ch = (unsigned char)body_prefix[bi];
            if (ch >= 0x20 || ch == '\n' || ch == '\t') {
                char s2[2]; s2[0]=(char)ch; s2[1]='\0'; print(s2);
            }
            body_bytes++;
        }}

        /* Streaming sisa body */
        char sbuf[256]; int sn;
        while ((sn = net_tcp_recv(conn, (uint8_t*)sbuf, 255)) > 0) {
            int si;
            for (si = 0; si < sn; si++) {
                unsigned char ch = (unsigned char)sbuf[si];
                if (ch >= 0x20 || ch == '\n' || ch == '\t') {
                    char s2[2]; s2[0]=(char)ch; s2[1]='\0'; print(s2);
                }
                body_bytes++;
            }
        }
        net_tcp_close(conn);

        /* Ringkasan */
        set_color(HTTP_COLOR_INFO, HTTP_COLOR_BG);
        print("\ncurl: selesai (");
        { char nb[10]; itoa((uint32_t)body_bytes, nb); print(nb); }
        print(" bytes body)\n");
        set_color(HTTP_COLOR_BODY, HTTP_COLOR_BG);
        return 0;
    }

    /* Terlalu banyak redirect */
    set_color(HTTP_COLOR_ERROR, HTTP_COLOR_BG);
    print("curl: terlalu banyak redirect (max 3)\n");
    set_color(HTTP_COLOR_BODY, HTTP_COLOR_BG);
    return -1;
}

/* ================================================================== */
/* F-AJ: https_get(url) — HTTPS GET via TLS 1.3                       */
/* url format: "https://hostname/path" atau "https://hostname"        */
/* ================================================================== */
#include "tls13.h"

int https_get(const char *url) {
    /* Parse: lewati "https://" */
    const char *p = url;
    if (p[0]=='h'&&p[1]=='t'&&p[2]=='t'&&p[3]=='p'&&p[4]=='s'&&p[5]==':'&&p[6]=='/'&&p[7]=='/')
        p += 8;
    else { print("https: URL harus mulai dengan https://\n"); return -1; }

    /* Ekstrak hostname dan path */
    char host[128]; int hi=0;
    while (*p && *p != '/' && hi < 127) host[hi++]=*p++;
    host[hi]=0;
    const char *path = (*p=='/') ? p : "/";

    /* DNS resolve */
    uint8_t ip[4];
    set_color(HTTP_COLOR_INFO, HTTP_COLOR_BG);
    print("https: resolving "); print(host); print("...\n");
    if (!dns_resolve(host, ip)) {
        set_color(HTTP_COLOR_ERROR, HTTP_COLOR_BG);
        print("https: gagal resolve hostname\n");
        set_color(HTTP_COLOR_BODY, HTTP_COLOR_BG);
        return -1;
    }

    /* TCP connect ke port 443 */
    set_color(HTTP_COLOR_INFO, HTTP_COLOR_BG);
    print("https: connecting port 443...\n");
    int conn = net_tcp_connect(ip, 443);
    if (conn < 0) {
        set_color(HTTP_COLOR_ERROR, HTTP_COLOR_BG);
        print("https: gagal TCP connect\n");
        set_color(HTTP_COLOR_BODY, HTTP_COLOR_BG);
        return -1;
    }

    /* TLS handshake */
    set_color(HTTP_COLOR_INFO, HTTP_COLOR_BG);
    print("https: TLS handshake...\n");
    if (tls13_connect(conn, host) != 0) {
        set_color(HTTP_COLOR_ERROR, HTTP_COLOR_BG);
        print("https: TLS handshake gagal\n");
        net_tcp_close(conn);
        set_color(HTTP_COLOR_BODY, HTTP_COLOR_BG);
        return -1;
    }
    set_color(HTTP_COLOR_INFO, HTTP_COLOR_BG);
    print("https: TLS established\n");

    /* Kirim HTTP request */
    static char req[512];
    int rp=0;
    /* GET path HTTP/1.0\r\nHost: host\r\nConnection: close\r\n\r\n */
    const char *method = "GET ";
    for(int i=0;method[i];i++) req[rp++]=method[i];
    for(int i=0;path[i];i++) req[rp++]=path[i];
    const char *proto = " HTTP/1.0\r\nHost: ";
    for(int i=0;proto[i];i++) req[rp++]=proto[i];
    for(int i=0;host[i];i++) req[rp++]=host[i];
    const char *tail = "\r\nConnection: close\r\n\r\n";
    for(int i=0;tail[i];i++) req[rp++]=tail[i];
    tls13_send(conn, req, rp);

    /* Terima respons */
    static uint8_t rbuf[4096];
    set_color(HTTP_COLOR_HEADER, HTTP_COLOR_BG);
    int body_started = 0;
    uint32_t body_bytes = 0;
    for (;;) {
        int got = tls13_recv(conn, rbuf, sizeof(rbuf)-1);
        if (got <= 0) break;
        rbuf[got] = 0;
        if (!body_started) {
            /* Cari \r\n\r\n */
            for (int i=0;i<got-3;i++) {
                if (rbuf[i]=='\r'&&rbuf[i+1]=='\n'&&rbuf[i+2]=='\r'&&rbuf[i+3]=='\n') {
                    /* Cetak header */
                    rbuf[i+4]=0;
                    print((char*)rbuf);
                    /* Mulai body */
                    set_color(HTTP_COLOR_BODY, HTTP_COLOR_BG);
                    body_started=1;
                    int bstart=i+4;
                    if (bstart<got) {
                        body_bytes+=got-bstart;
                        rbuf[got]=0;
                        print((char*)rbuf+bstart);
                    }
                    break;
                }
            }
            if (!body_started) { print((char*)rbuf); }
        } else {
            body_bytes+=got;
            print((char*)rbuf);
        }
    }

    tls13_close(conn);
    net_tcp_close(conn);

    set_color(HTTP_COLOR_INFO, HTTP_COLOR_BG);
    print("\nhttps: selesai (");
    { char nb[10]; itoa(body_bytes, nb); print(nb); }
    print(" bytes body)\n");
    set_color(HTTP_COLOR_BODY, HTTP_COLOR_BG);
    return 0;
}

/* ================================================================
 * Fondasi AL — NTP Client
 * Kirim NTP v3 request ke pool.ntp.org:123 via UDP.
 * Parse Transmit Timestamp, tampilkan waktu.
 * Return 1 sukses, 0 gagal.
 * ================================================================ */
#define NTP_PORT      123
#define NTP_SRC_PORT  1123
/* NTP epoch = 1 Jan 1900; Unix epoch = 1 Jan 1970.
 * Selisih: 70 tahun = 2208988800 detik */
#define NTP_UNIX_DIFF 2208988800UL

int ntp_sync(void) {
    if (!rtl8139_present()) {
        print("ntp: NIC tidak tersedia\n"); return 0;
    }

    /* Resolve pool.ntp.org */
    static const uint8_t ntp_fallback[4] = {216, 239, 35, 0}; /* time.google.com */
    uint8_t ntp_ip[4];
    print("ntp: resolve pool.ntp.org... ");
    if (!dns_resolve("pool.ntp.org", ntp_ip)) {
        ntp_ip[0] = ntp_fallback[0]; ntp_ip[1] = ntp_fallback[1];
        ntp_ip[2] = ntp_fallback[2]; ntp_ip[3] = ntp_fallback[3];
        print("timeout, pakai 216.239.35.0\n");
    } else {
        { char ib[16]; int ii;
          for (ii = 0; ii < 4; ii++) {
              char nb[4]; itoa(ntp_ip[ii], nb); print(nb);
              if (ii < 3) print(".");
          }
          (void)ib;
        }
        print("\n");
    }

    /* Bangun NTP request packet (48 byte, RFC 5905) */
    uint8_t pkt[48];
    int k;
    for (k = 0; k < 48; k++) pkt[k] = 0;
    pkt[0] = 0x1B; /* LI=0, VN=3, Mode=3 (client) */

    /* Kirim 3 kali, tunggu reply */
    int attempt;
    for (attempt = 0; attempt < 3; attempt++) {
        net_udp_send(ntp_ip, NTP_PORT, NTP_SRC_PORT, pkt, 48);

        uint32_t deadline = get_ticks() + 3000;
        while (get_ticks() < deadline) {
            uint8_t rx[1514]; uint16_t rlen;
            if (rtl8139_recv(rx, &rlen) == 0 && rlen > 90) {
                uint8_t *ip  = rx + 14;
                if (ip[9] != 17) { net_process(rx, rlen, 0, 0, 0); continue; }
                uint8_t *udp = ip + (ip[0] & 0xF) * 4;
                uint16_t sp = (uint16_t)(((uint16_t)udp[0] << 8) | udp[1]);
                uint16_t dp = (uint16_t)(((uint16_t)udp[2] << 8) | udp[3]);
                if (sp != NTP_PORT || dp != NTP_SRC_PORT) {
                    net_process(rx, rlen, 0, 0, 0); continue;
                }
                uint8_t *np = udp + 8;
                /* Transmit Timestamp: byte 40-47 (NTP epoch seconds di [40..43]) */
                uint32_t ntp_secs = ((uint32_t)np[40] << 24) | ((uint32_t)np[41] << 16)
                                  | ((uint32_t)np[42] << 8)  |  (uint32_t)np[43];
                if (ntp_secs < NTP_UNIX_DIFF) {
                    /* Terima juga Originate/Receive timestamp */
                    ntp_secs = ((uint32_t)np[32] << 24) | ((uint32_t)np[33] << 16)
                             | ((uint32_t)np[34] << 8)  |  (uint32_t)np[35];
                }
                if (ntp_secs == 0) { net_process(rx, rlen, 0, 0, 0); continue; }

                uint32_t unix_secs = ntp_secs - NTP_UNIX_DIFF;

                /* Konversi Unix timestamp ke tahun/bulan/hari/jam/menit/detik */
                uint32_t s = unix_secs % 86400;
                uint32_t d = unix_secs / 86400;       /* hari sejak 1 Jan 1970 */
                uint32_t hour   = s / 3600;
                uint32_t minute = (s % 3600) / 60;
                uint32_t second = s % 60;

                /* Hitung tahun dan hari dalam tahun */
                uint32_t year = 1970;
                while (1) {
                    int leap = ((year%4==0 && year%100!=0) || year%400==0);
                    uint32_t days_in_year = (uint32_t)(leap ? 366 : 365);
                    if (d < days_in_year) break;
                    d -= days_in_year;
                    year++;
                }
                static const uint8_t mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
                uint32_t month = 1;
                {
                    int leap = ((year%4==0 && year%100!=0) || year%400==0);
                    int m;
                    for (m = 0; m < 12; m++) {
                        uint32_t md = mdays[m];
                        if (m == 1 && leap) md = 29;
                        if (d < md) { month = (uint32_t)(m + 1); break; }
                        d -= md;
                    }
                }
                uint32_t day = d + 1;

                /* Tampilkan hasil */
                set_color(0x0055FF55, 0);
                print("ntp: OK  ");
                { char nb[8]; itoa(year, nb); print(nb); }
                print("-");
                { char nb[4]; if (month < 10) print("0"); itoa(month, nb); print(nb); }
                print("-");
                { char nb[4]; if (day   < 10) print("0"); itoa(day,   nb); print(nb); }
                print(" ");
                { char nb[4]; if (hour   < 10) print("0"); itoa(hour,   nb); print(nb); }
                print(":");
                { char nb[4]; if (minute < 10) print("0"); itoa(minute, nb); print(nb); }
                print(":");
                { char nb[4]; if (second < 10) print("0"); itoa(second, nb); print(nb); }
                print(" UTC  (unix=");
                { char nb[12]; itoa(unix_secs, nb); print(nb); }
                print(")\n");
                set_color(0x00FFFFFF, 0);
                return 1;
            }
        }
    }
    set_color(0x00FF5555, 0);
    print("ntp: timeout — tidak ada balasan\n");
    set_color(0x00FFFFFF, 0);
    return 0;
}

/* ================================================================== */
/* Fondasi AP — IPv6 + ICMPv6 ping6                                  */
/* ================================================================== */

/* Link-local IPv6 guest: fe80:: + EUI-64 dari MAC */
static uint8_t MY_IP6[16];
static int     my_ip6_ready = 0;

/* MAC gateway IPv6 (fe80::2 → QEMU SLIRP, resolved via NDP) */
static uint8_t gw_mac6[6];
static int     gw_mac6_valid = 0;

/* Hitung EUI-64 link-local dari MAC dan simpan ke MY_IP6 */
static void ip6_make_link_local(void) {
    /* fe80::/10 prefix */
    MY_IP6[0] = 0xFE; MY_IP6[1] = 0x80;
    MY_IP6[2]=0; MY_IP6[3]=0; MY_IP6[4]=0; MY_IP6[5]=0;
    MY_IP6[6]=0; MY_IP6[7]=0;
    /* EUI-64: flip U/L bit, masukkan FF:FE di tengah MAC */
    MY_IP6[8]  = my_mac[0] ^ 0x02;
    MY_IP6[9]  = my_mac[1];
    MY_IP6[10] = my_mac[2];
    MY_IP6[11] = 0xFF;
    MY_IP6[12] = 0xFE;
    MY_IP6[13] = my_mac[3];
    MY_IP6[14] = my_mac[4];
    MY_IP6[15] = my_mac[5];
    my_ip6_ready = 1;
}

/* Checksum ICMPv6 dengan pseudo-header IPv6 */
static uint16_t icmp6_checksum(const uint8_t src6[16], const uint8_t dst6[16],
                                const uint8_t *payload, uint16_t plen) {
    uint32_t sum = 0;
    int i;
    /* Pseudo-header: src (16) + dst (16) + length (4) + zeros(3) + NH=58 (1) */
    for (i = 0; i < 16; i += 2)
        sum += (uint32_t)(((uint16_t)src6[i] << 8) | src6[i+1]);
    for (i = 0; i < 16; i += 2)
        sum += (uint32_t)(((uint16_t)dst6[i] << 8) | dst6[i+1]);
    sum += (uint32_t)plen;
    sum += 58; /* Next Header ICMPv6 */
    /* Payload */
    for (i = 0; i+1 < (int)plen; i += 2)
        sum += (uint32_t)(((uint16_t)payload[i] << 8) | payload[i+1]);
    if (plen & 1) sum += (uint32_t)((uint16_t)payload[plen-1] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

/* Bangun dan kirim frame Ethernet + IPv6 + ICMPv6 */
static void ip6_send(const uint8_t dst_mac[6], const uint8_t dst6[16],
                     uint8_t next_hdr, const uint8_t *payload, uint16_t plen) {
    uint8_t frame[1514];
    uint8_t *p = frame;
    int i;
    /* Ethernet header */
    for (i=0; i<6; i++) p[i]   = dst_mac[i];
    for (i=0; i<6; i++) p[6+i] = my_mac[i];
    p[12] = 0x86; p[13] = 0xDD;   /* EtherType IPv6 */
    p += 14;
    /* IPv6 fixed header (40 bytes) */
    p[0] = 0x60; p[1]=0; p[2]=0; p[3]=0;   /* version=6, TC=0, flow=0 */
    p[4] = (uint8_t)(plen >> 8);
    p[5] = (uint8_t)(plen & 0xFF);           /* payload length */
    p[6] = next_hdr;
    p[7] = 64;                               /* hop limit */
    for (i=0; i<16; i++) p[8+i]  = MY_IP6[i];
    for (i=0; i<16; i++) p[24+i] = dst6[i];
    p += 40;
    /* Payload */
    for (i=0; i<(int)plen; i++) p[i] = payload[i];
    rtl8139_send(frame, (uint16_t)(14 + 40 + plen));
}

/* Multicast MAC untuk IPv6: 33:33 + last 4 bytes IPv6 */
static void ip6_mcast_mac(const uint8_t ip6[16], uint8_t mac[6]) {
    mac[0]=0x33; mac[1]=0x33;
    mac[2]=ip6[12]; mac[3]=ip6[13]; mac[4]=ip6[14]; mac[5]=ip6[15];
}

/* Solicited-node multicast: FF02::1:FF<last3> */
static void ip6_solicited_node(const uint8_t target[16], uint8_t snm[16]) {
    snm[0]=0xFF; snm[1]=0x02;
    snm[2]=0; snm[3]=0; snm[4]=0; snm[5]=0;
    snm[6]=0; snm[7]=0; snm[8]=0; snm[9]=0;
    snm[10]=0; snm[11]=0x01;
    snm[12]=0xFF;
    snm[13]=target[13]; snm[14]=target[14]; snm[15]=target[15];
}

/* Kirim NDP Neighbor Solicitation untuk target */
static void ndp_send_ns(const uint8_t target6[16]) {
    uint8_t snm[16], dst_mac[6];
    uint8_t icmp6[32];
    uint16_t csum;
    int i;
    ip6_solicited_node(target6, snm);
    ip6_mcast_mac(snm, dst_mac);
    /* ICMPv6 NS: type=135, code=0, cksum(placeholder), reserved(4), target(16),
     * option: type=1(src link-layer), len=1(8 bytes), MAC(6) */
    icmp6[0]=135; icmp6[1]=0; icmp6[2]=0; icmp6[3]=0; /* type,code,cksum */
    icmp6[4]=0;icmp6[5]=0;icmp6[6]=0;icmp6[7]=0;       /* reserved */
    for (i=0;i<16;i++) icmp6[8+i]=target6[i];           /* target */
    icmp6[24]=1; icmp6[25]=1;                            /* option type=1, len=1(×8) */
    for (i=0;i<6;i++) icmp6[26+i]=my_mac[i];            /* source MAC */
    csum = icmp6_checksum(MY_IP6, snm, icmp6, 32);
    icmp6[2]=(uint8_t)(csum>>8); icmp6[3]=(uint8_t)(csum&0xFF);
    ip6_send(dst_mac, snm, 58, icmp6, 32);
}

/* Resolve IPv6 → MAC via NDP, simpan ke out_mac. Return 1 sukses, 0 timeout */
static int ndp_resolve(const uint8_t target6[16], uint8_t out_mac[6]) {
    uint8_t rbuf[1514]; uint16_t rlen;
    uint32_t deadline;
    int attempt, i;
    /* Cek cache gateway */
    if (gw_mac6_valid) {
        int same = 1;
        for (i=0;i<16;i++) if (target6[i] != ((uint8_t[]){0xfe,0x80,0,0,0,0,0,0,0,0,0,0,0,0,0,0x02})[i]) { same=0; break; }
        if (same) { for(i=0;i<6;i++) out_mac[i]=gw_mac6[i]; return 1; }
    }
    for (attempt=0; attempt<3; attempt++) {
        ndp_send_ns(target6);
        deadline = get_ticks() + 1500;
        while (get_ticks() < deadline) {
            if (rtl8139_recv(rbuf, &rlen) == 0 && rlen >= 86) {
                /* Check: EtherType=0x86DD, IPv6, ICMPv6 type=136 (NA) */
                if (rbuf[12]==0x86 && rbuf[13]==0xDD &&
                    (rbuf[14]>>4)==6 &&
                    rbuf[20]==58 /* next hdr ICMPv6 */ &&
                    rbuf[54]==136 /* NA */)
                {
                    /* target addr di NA: offset 14+40+8 = 62 */
                    int match = 1;
                    for (i=0;i<16;i++) if (rbuf[62+i]!=target6[i]) { match=0; break; }
                    if (match) {
                        /* Option type=2(target link-layer) offset 78 */
                        if (rlen >= 86 && rbuf[78]==2 && rbuf[79]==1) {
                            for(i=0;i<6;i++) out_mac[i]=rbuf[80+i];
                            /* simpan jika ini gateway */
                            {
                                int isgw=1;
                                static const uint8_t GW6[16]={0xfe,0x80,0,0,0,0,0,0,0,0,0,0,0,0,0,0x02};
                                for(i=0;i<16;i++) if(target6[i]!=GW6[i]){isgw=0;break;}
                                if(isgw){ for(i=0;i<6;i++) gw_mac6[i]=out_mac[i]; gw_mac6_valid=1; }
                            }
                            return 1;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

/* Public: ping6 ke dst_ip6 sebanyak count kali. */
int net_ping6(const uint8_t dst_ip6[16], int count) {
    uint8_t icmp6[16], dst_mac[6];
    uint8_t rbuf[1514]; uint16_t rlen;
    uint16_t csum;
    int seq, i;
    if (!rtl8139_present()) { print("ping6: NIC tidak ada\n"); return -1; }
    if (!my_ip6_ready) ip6_make_link_local();

    /* Resolve MAC destination */
    if (!ndp_resolve(dst_ip6, dst_mac)) {
        /* Fallback: gunakan multicast MAC agar SLIRP tetap menerima */
        ip6_mcast_mac(dst_ip6, dst_mac);
    }

    for (seq = 1; seq <= count; seq++) {
        uint32_t t0, deadline;
        /* ICMPv6 Echo Request type=128 */
        icmp6[0]=128; icmp6[1]=0; icmp6[2]=0; icmp6[3]=0;
        icmp6[4]=0x6F; icmp6[5]=0x72; /* ID = 'or' */
        icmp6[6]=(uint8_t)(seq>>8); icmp6[7]=(uint8_t)(seq&0xFF);
        icmp6[8]=0x61; icmp6[9]=0x70; icmp6[10]=0x36; icmp6[11]=0; /* data */
        icmp6[12]=0; icmp6[13]=0; icmp6[14]=0; icmp6[15]=0;
        csum = icmp6_checksum(MY_IP6, dst_ip6, icmp6, 16);
        icmp6[2]=(uint8_t)(csum>>8); icmp6[3]=(uint8_t)(csum&0xFF);
        t0 = get_ticks();
        ip6_send(dst_mac, dst_ip6, 58, icmp6, 16);

        /* Tunggu ICMPv6 Echo Reply */
        deadline = t0 + 3000;
        while (get_ticks() < deadline) {
            if (rtl8139_recv(rbuf, &rlen) == 0 && rlen >= 62) {
                if (rbuf[12]==0x86 && rbuf[13]==0xDD &&
                    (rbuf[14]>>4)==6 && rbuf[20]==58 &&
                    rbuf[54]==129 /* Echo Reply */)
                {
                    /* Cek ID cocok ('or' = 0x6F72) */
                    if (rbuf[58]==0x6F && rbuf[59]==0x72) {
                        uint32_t rtt = get_ticks() - t0;
                        /* Cetak: ping6 reply dari <addr> seq=N rtt=Xms */
                        set_color(0x0055FF55, 0);
                        print("ping6: reply dari [");
                        for (i=0;i<16;i++) {
                            if (i>0 && (i%2)==0) print(":");
                            {
                                char hb[3];
                                hb[0]="0123456789abcdef"[rbuf[22+i]>>4];
                                hb[1]="0123456789abcdef"[rbuf[22+i]&0xF];
                                hb[2]=0;
                                print(hb);
                            }
                        }
                        print("]  seq=");
                        { char nb[8]; itoa((uint32_t)seq, nb); print(nb); }
                        print("  rtt=");
                        { char nb[8]; itoa(rtt, nb); print(nb); }
                        print(" ms\n");
                        set_color(0x00FFFFFF, 0);
                        goto next_seq;
                    }
                }
                /* Tangani NDP NS masuk jika ada (respond to neighbor solicitation) */
                if (rbuf[12]==0x86 && rbuf[13]==0xDD &&
                    (rbuf[14]>>4)==6 && rbuf[20]==58 &&
                    rbuf[54]==135 /* NS */)
                {
                    /* Balas dengan NA — opsional, SLIRP biasanya tidak butuh ini */
                }
            }
        }
        set_color(0x00FF5555, 0);
        print("ping6: timeout  seq=");
        { char nb[8]; itoa((uint32_t)seq, nb); print(nb); }
        print("\n");
        set_color(0x00FFFFFF, 0);
        next_seq:;
    }
    return 0;
}
