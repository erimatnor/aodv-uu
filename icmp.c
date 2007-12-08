#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip.h>

#include "defs.h"
#include "debug.h"

#define ICMP_BUFSIZE sizeof(struct icmphdr) + 60 + 20

char icmp_send_buf[ICMP_BUFSIZE];
int icmp_socket;

static unsigned short cksum(unsigned short *w, int len) {
  int sum = 0;
  unsigned short answer = 0;
  
  while(len > 1) {
    sum += *w++;
    len -= 2;
  }
  
  if(len == 1) {
    *(unsigned char *)(&answer) = *(unsigned char *)w;
    sum += answer;
  }
  sum = (sum >> 16) + (sum & 0xffff);
  sum += (sum >> 16);
  answer = ~sum;

  return(answer);
}

/* Data = IP header + 64 bits of data */
int icmp_send_host_unreachable(u_int32_t dest, char *data, int len) {
  struct icmphdr *icmp;
  struct sockaddr_in dst_addr;
  int ret, icmp_socket;
  char tos = IPTOS_PREC_INTERNETCONTROL;

  icmp_socket = socket(PF_INET, SOCK_RAW, IPPROTO_ICMP);
  if(icmp_socket < 0)
    return -1;
  
  setsockopt(icmp_socket, SOL_IP, IP_TOS, &tos, sizeof(char));
  
  log(LOG_DEBUG, 0, "icmp_send: Sending HOST_UNREACHABLE to %s, len=%d", 
      ip_to_str(dest), len);

  memset(icmp_send_buf, 0, ICMP_BUFSIZE);

  icmp = (struct icmphdr *)icmp_send_buf;
  
  icmp->type = ICMP_DEST_UNREACH; 
  icmp->code = ICMP_HOST_UNREACH; 

  memcpy(icmp_send_buf + sizeof(struct icmphdr), data, len);
 
  icmp->checksum = cksum((u_short *)icmp, len + sizeof(struct icmphdr));
 
  dst_addr.sin_family = AF_INET;
  dst_addr.sin_addr.s_addr = htonl(dest);
  dst_addr.sin_port = htons(INADDR_ANY);
  
  ret = sendto(icmp_socket, icmp_send_buf, len + sizeof(struct icmphdr), 0, 
	       (struct sockaddr *)&dst_addr, sizeof(dst_addr));

  close(icmp_socket);
  
  return ret;
}
