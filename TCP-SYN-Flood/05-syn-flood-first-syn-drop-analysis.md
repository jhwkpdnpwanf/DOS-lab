# timeout 값 변화에 따른 SYN First Drop 방어 성능 영향 분석


### First SYN Drop   
- 처음 온 연결 요청은 버리고 다음 연결부터 받는 방법입니다.  
- SYN flooding의 경우 가짜 주소로 수많은 SYN 패킷만을 보내므로 가짜 요청을 걸리내기 위한 방어기법입니다.  

<br>

### hook 위치 & 선택이유  
PREROUTING hook을 사용할 예정입니다.   

SYN flood와 같이 대량의 패킷이 유입되는 공격을 조기에 차단하고 conntrack 테이블의 불필요한 부하를 최소화하고,
timeout 값에 따른 변화를 관찰하기 위해서 입니다.  

또한 PREROUTING은 conntrack의 시작점이므로 (nf_conntack_in 함수로 시작) 타임아웃 설정을 변경하여 그 영향을 측정한다면 지연시간의 왜곡을 줄일 수 있습니다.   

<br>

### 실험 방법   
- `timeout` 값을 바꿔가며 실험을 진행합니다.  
- SYN 패킷은 1분간 전송합니다.  
- 이때 패킷을 받아들인 횟수와 drop한 횟수를 세어보며 

<br>

우선 테이블을 구성하기 위해 아래 경로에 파일을 생성해줍시다.  

```
vi /etc/nftables.d/syn_first_drop.nft
```
<br>


**syn_first_drop.nft**  

```nft
table inet syn_first_drop {
    set syn_seen {
        type ipv4_addr . inet_service . inet_service
        flags timeout
    }

    counter syn_retry_accept {}
    counter first_syn_drop {}

    chain preraw {
        type filter hook prerouting priority -300; policy accept;

        ip daddr 192.168.125.145 tcp flags & (syn|ack|rst) == syn ip saddr . tcp sport . tcp dport @syn_seen counter name "syn_retry_accept" accept
        ip daddr 192.168.125.145 tcp flags & (syn|ack|rst) == syn add @syn_seen { ip saddr . tcp sport . tcp dport timeout 1s } counter name "first_syn_drop" drop
    }
}
```

우선 두번째로 오는 SYN 패킷을 알아내기 위해서는 첫번째 패킷에 대한 몇가지 정보를 기억하고 있어야합니다. 따라서 `syn_seen`을 timeout 플래그가 적용된 상태에서 선언해줍시다.  

이후 첫 SYN 패킷을 관측하면 출발 ip 주소, 출발 포트, 도착 포트를 기억할 수 있도록 `ipv4_addr` `inet_service` `inet_service`로 키를 구성해줍니다.  

이번 실습에서는 도착주소가 192.168.125.145 하나이므로 도착 ip 주소는 따로 세팅하지 않았습니다.   


192.168.125.145 주소로 패킷이 주소하면, 이전에 적용해둔 리다이렉트 규칙에 따라 metasploitable(192.168.1.22)으로 패킷이 흘러가므로, 192.168.125.145 주소로 가는 패킷에 대해 SYN 단독 패킷만 적용하도록 해줍시다.  
- `ip daddr 192.168.125.145 tcp flags & (syn|ack|rst) == syn`  

<br>

동일한 `출발 ip 주소, 출발 포트, 도착 포트`가 syn_seen에 있다면 허용하고, 그렇지 않다면 drop하도록 해줍니다.  



<br>

이제 메인 설정에 추가할 수 있도록,  
```sh
vi /etc/nftables.conf
```  

아래 줄을 추가해주면 됩니다.  
```nft
include "/etc/nftables.d/syn_first_drop.nft"
```  

그리고 적용을 해주면 준비는 끝입니다.  
```nft
nft -f /etc/nftables.conf
```


counter 개수를 세고, reset하는 것은 아래 명령어를 치시면 됩니다.  
```sh
nft list counters
nft reset counters
```
  
timeset 시간이 끝나면 자동으로 drop되지만 가독성을 위해 delete 후 재적용하는 방식을 사용했습니다.   

```sh
nft delete table inet syn_first_drop
nft -f /etc/nftables.conf
```

<br>


### timeout = 1s 일 때   

**정상 사용자 접속가능 확인**  
![1초](./img/first-syn-drop2.png)

**1분 후 counter 상황**  
![alt text](./img/first-syn-drop3.png)

<br>

### timeout = 3s 일 때   

**정상 사용자 접속가능 확인**  
![3초](./img/first-syn-drop.png)

**1분 후 counter 상황**  
![3초](./img/first-syn-drop1.png)
```
nft delete table inet syn_first_drop
nft -f /etc/nftables.conf
```

<br>

### timeout = 5s 일 때   

**정상 사용자 접속가능 확인**  
![5초](./img/first-syn-drop4.png)

**1분 후 counter 상황**   
![5초](./img/first-syn-drop5.png)


<br>


### 상황 설명 & 결론   

| timeout (초) | syn_retry_accept (개) | first_syn_drop (개) | 전체 SYN (개) | 재시도 SYN 허용 비율 | 첫 SYN drop 비율 |
| -- | -------------------: | -----------------: | ---------: | ------------: | ----------: |
| 1  |                1,798 |             47,616 |     49,414 |         3.64% |      96.36% |
| 3  |                1,825 |             47,711 |     49,536 |         3.68% |      96.32% |
| 5  |                2,929 |             47,505 |     50,434 |         5.81% |      94.19% |


<br>

랜덤한 포트로 공격을 하는 방식에서, 대부분의 공격이 걸러지는 효과가 나타났습니다. timeout 시간에 따라서 허용비율이 올라가는 것을 확인해볼 수 있으며 직관적으로 예상할 수 있는 결과가 나타났습니다.  

현재의 환경에 따르면 1초와 3초의 비율이 매우 비슷하게 나타납니다. 이는 대부분의 syn 재전송이 1초 이내에 유입된다는 것을 알 수 있습니다. timeout을 늘릴수록 정상 사용자의 느린 syn 재전송까지 수용할 수 있다는 이점이 있으나, 동시에 공격자의 패킷 또한 통과 가능성이 증가할 수 있음을 의미합니다.  

결론적으로 first syn drop의 timeout 값에 따른 분류 정확도는 일정 수치 범위 내에서는 (현재 실험에서는 1~5s) 눈에 띄는 차이가 없다고 생각합니다. 중요한 것은 openwrt 에서의 패킷퍼리 부담이 줄었는지에 대한 확인이 필요해보입니다.  

timeout이 짧을수록 `syn_seen` 엔트리가 빨리 사라지므로 상태 유지 비용이 낮을 것입니다. 반면 timeout이 길수록 살아있는 `syn_seen` 엔트리가 증가할 것이므로 공격자/정상사용자 의 SYN 재전송의 처리비용이 증가할 것입니다. 이는 부하 증가 가능성을 시사하며, 이러한 부분을 관측하는 것이 중요하다고 여겨집니다.  

따라서 라우터의 변화를 추가로 살펴보며 syn fitst drop의 방어 성능을 살펴보겠습니다.  