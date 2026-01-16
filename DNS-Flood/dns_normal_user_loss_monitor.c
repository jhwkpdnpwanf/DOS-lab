#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#define DNS_SERVER_IP "192.168.125.10"
#define DNS_PORT 53

#define MAX_PACKETS 1000 // 총 전송할 패킷 수
#define SEND_INTERVAL_MS 200 // 전송 간격 (ms)
#define RCV_TIMEOUT_MS   200 // 수신 타임아웃 (ms)
#define WINDOW_SEC 10 // 집계 윈도우 길이 (sec)

/* DNS header 구조체 */
#pragma pack(push, 1)
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;
#pragma pack(pop)

// 현재 시간(ms) 반환
static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000LL);
}

// 성공 조건: 
// - ID 일치
// - 응답 (QR=1)
// - RCODE=0 (NOERROR)
// - ANCOUNT >= 1
static int dns_is_ok_answer(const unsigned char *buf, int len, uint16_t expect_id) {
    if (len < (int)sizeof(dns_header_t)) return 0;

    dns_header_t h;
    memcpy(&h, buf, sizeof(h));

    uint16_t id = ntohs(h.id);
    if (id != expect_id) return 0;

    uint16_t flags = ntohs(h.flags);
    int qr = (flags >> 15) & 1;
    int rcode = flags & 0x0F;

    if (qr != 1) return 0; // QR = 1 이여야 함
    if (rcode != 0) return 0; // NOERROR만 성공

    uint16_t an = ntohs(h.ancount);
    if (an < 1) return 0; // Answer 없으면(ancount=0) 성공으로 치지 않음

    return 1;
}

int main(void) {
    int msg = socket(AF_INET, SOCK_DGRAM, 0);
    if (msg < 0) {
        perror("socket");
        return 1;
    }

    // recvfrom 타임아웃 설정
    struct timeval rcv_to;
    rcv_to.tv_sec  = RCV_TIMEOUT_MS / 1000;
    rcv_to.tv_usec = (RCV_TIMEOUT_MS % 1000) * 1000;
    if (setsockopt(msg, SOL_SOCKET, SO_RCVTIMEO, &rcv_to, sizeof(rcv_to)) < 0) {
        perror("setsockopt(SO_RCVTIMEO)");
        close(msg);
        return 1;
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(DNS_PORT);
    dest.sin_addr.s_addr = inet_addr(DNS_SERVER_IP);

    // DNS header
    unsigned char packet[64] = {
        0x00, 0x00, // ID (매번 변경)
        0x01, 0x00, // Flags: RD=1
        0x00, 0x01, // QDCOUNT=1
        0x00, 0x00, // ANCOUNT=0
        0x00, 0x00, // NSCOUNT=0
        0x00, 0x00  // ARCOUNT=0
    };  
    
    // DNS query
    unsigned char query[] = {
        0x03, 'd','o','s',
        0x03, 'l','a','b',
        0x00,
        0x00, 0x01, // Type: A
        0x00, 0x01  // Class: IN
    };
    memcpy(packet + 12, query, sizeof(query));
    size_t pkt_len = 12 + sizeof(query);

    // 송신 간격
    struct timespec ts_sleep;
    ts_sleep.tv_sec = 0;
    ts_sleep.tv_nsec = (long)SEND_INTERVAL_MS * 1000L * 1000L;

    // 윈도우 집계
    const int window_sec = WINDOW_SEC;
    long long window_start_ms = now_ms();

    unsigned long win_sent = 0;
    unsigned long win_recv_ok = 0;
    unsigned long win_bad_resp = 0; // 잘못된 응답
    unsigned long win_timeout = 0;
    unsigned long win_err = 0;

    time_t start_time = time(NULL);
    printf("Start time: %s", ctime(&start_time));
    printf("Target DNS: %s:%d\n", DNS_SERVER_IP, DNS_PORT);
    printf("Send interval: %dms (%.2f QPS)\n", SEND_INTERVAL_MS, 1000.0 / (double)SEND_INTERVAL_MS);
    printf("Recv timeout: %dms\n", RCV_TIMEOUT_MS);
    printf("Window: %ds\n\n", window_sec);

    for (int i = 0; i < MAX_PACKETS; i++) {
        uint16_t dns_id = (uint16_t)i;
        *(uint16_t *)packet = htons(dns_id);

        ssize_t s = sendto(msg, packet, pkt_len, 0, (struct sockaddr*)&dest, sizeof(dest));
        if (s < 0) {
            perror("sendto");
            win_err++;
        } else {
            win_sent++;
        }

        // 응답 수신
        unsigned char buffer[512];
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);

        ssize_t res_len = recvfrom(msg, buffer, sizeof(buffer), 0, (struct sockaddr*)&from, &from_len);

        if (res_len > 0) {
            // 응답 도착
            if (dns_is_ok_answer(buffer, (int)res_len, dns_id)) {
                win_recv_ok++;
            } else {
                // 응답은 왔지만 실패 응답
                win_bad_resp++;
            }
        } else {
            // 타임아웃
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                win_timeout++;
            } else {
            // 기타 오류
                win_err++;
            }
        }

        // 윈도우 출력
        long long now = now_ms();
        if (now - window_start_ms >= (long long)window_sec * 1000LL) {
            unsigned long loss = (win_sent >= win_recv_ok) ? (win_sent - win_recv_ok) : 0;
            double loss_rate = (win_sent > 0) ? (100.0 * (double)loss / (double)win_sent) : 0.0;

            printf("[Last %ds] sent=%lu, ok=%lu, bad_resp=%lu, timeout=%lu, err=%lu, loss=%lu (%.2f%%)\n",
                   window_sec, win_sent, win_recv_ok, win_bad_resp, win_timeout, win_err, loss, loss_rate);

            window_start_ms = now;
            win_sent = win_recv_ok = win_bad_resp = win_timeout = win_err = 0;
        }

        nanosleep(&ts_sleep, NULL);
    }

    close(msg);
    return 0;
}
