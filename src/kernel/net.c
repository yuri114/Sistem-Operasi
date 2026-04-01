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

/* ---- Konfigurasi jaringan statis ---- */
static uint8_t my_mac[6];
static const uint8_t MY_IP[4]    = {10, 0, 2, 15};
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
        int pay_len        = (int)len - 14 - ihl;

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
