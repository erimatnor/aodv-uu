#ifndef LINK_QUAL_H
#define LINK_QUAL_H

void link_qual_init(char *spy_addrs);
void link_qual_cleanup(void);

int link_qual_get_from_ip(struct in_addr ip_addr, char *ifname);

#endif
