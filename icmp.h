#ifndef ICMP_H
#define ICMP_H

void icmp_init();

int icmp_send_host_unreachable(u_int32_t dest, char *data, int len);


#endif
