#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>

int main() {
    int msg = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in dest;
    dest.sin_family = AF_INET;
    dest.sin_port = htons(53);
    dest.sin_addr.s_addr = inet_addr("192.168.125.10"); // 실습용 dns 서버


    unsigned char packet[64] = {
        0x00, 0x00, // 임시 id (랜덤으로 매번 변경 예정): 0x0000
        0x01, 0x00, // Flags: RD = 1
        0x00, 0x01, // Questions 카운트: 1
        0x00, 0x00, // Answer 카운트: 0
        0x00, 0x00, // 네임서버 카운트: 0
        0x00, 0x00  // 부가정보 레코즈 카운터: 0
    };


    unsigned char query[] = {
        0x03, 'd','o','s',
        0x03, 'l','a','b', 
        0x00,
        0x00, 0x01, // Type: A (IPv4)
        0x00, 0x01  // Class: IN
    };

    srand(time(NULL));

    while(1){
        // 0x04 , < 랜덤 4바이트 > 생성
        unsigned char buf[5];
        buf[0] = 0x04;
        for (int i = 1; i < 5; i++) {
            buf[i] = 'a' + (rand() % 26); 
        }
        
        memcpy(packet + 12, buf, 5);
        memcpy(packet + 17, query, sizeof(query));
        
        // id 값은 매번 랜덤으로 변경
        uint16_t dns_id = (uint16_t)(rand() & 0xFFFF);
        *(uint16_t *)packet = htons(dns_id);

        // 쿼리 전송
        sendto(msg, packet, 12 + sizeof(query), 0, (struct sockaddr*)&dest, sizeof(dest));
        printf("Query Sent - id: 0x%04X\n", dns_id);
        // 응답 확인
        unsigned char buffer[512];
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);

        int res_len = recvfrom(msg, buffer, sizeof(buffer), 0, (struct sockaddr*)&from, &from_len);

        if (res_len > 0) {
            // 응답 패킷의 id를 알기 위해 처음 2바이트를 추출
            uint16_t res_id = ntohs(*(uint16_t *)buffer);
            printf("Received id: 0x%04X\n", res_id);
            // 응답받은 ip 주소를 출력하기 위해 마지막 4바이트 추출
            if (res_len >= 4) {
                printf("IP Address: %d.%d.%d.%d\n",
                    buffer[res_len-4], buffer[res_len-3], 
                    buffer[res_len-2], buffer[res_len-1]);
            }
        } else {
            printf("No response.\n");
        }
    }

    close(msg);

    return 0;
}