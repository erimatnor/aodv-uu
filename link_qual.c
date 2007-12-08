#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/wireless.h>
#include <string.h>
#include <net/if_arp.h>
#include <netinet/if_ether.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>

#include "defs.h"
#include "debug.h"

static int iwsock = 0;

struct ip_mac {
    struct in_addr ip;
    char mac[ETH_ALEN];
};

/* An array with IP-MAC pairs so that we can translate addresses back
 * and forth... */
static struct ip_mac ip2mac_table[IW_MAX_SPY];
static int nipmac = 0;
static int link_qual_add_spy_by_ip(struct sockaddr *ip_addr, char *ifname);
static int mac_to_bin(char *bufp, unsigned char *mac);
static int link_qual_add_spy(struct sockaddr *mac_addr, char *ifname);

static void clear_spy_lists(void)
{
    int i;
    struct iwreq wrq;

    /* Clear spy list of all AODV interfaces */
    for (i = 0; i < MAX_NR_INTERFACES; i++) {
	if (!DEV_NR(i).enabled)
	    continue;

	wrq.u.data.pointer = NULL;
	wrq.u.data.length = 0;
	wrq.u.data.flags = 0;

	strncpy(wrq.ifr_name, DEV_NR(i).ifname, IFNAMSIZ);
	ioctl(iwsock, SIOCSIWSPY, &wrq);
    }
}

void link_qual_init(char *spy_addrs)
{
    char *str;
    char *ifname = "eth0";
    int i;

    iwsock = socket(PF_INET, SOCK_DGRAM, 0);

    if (iwsock < 0) {
	fprintf(stderr, "Could not create wireless socket!\n");
	exit(-1);
    }

    memset(ip2mac_table, 0, IW_MAX_SPY * sizeof(struct ip_mac));
    nipmac = 0;

    clear_spy_lists();

    /* Hmm, use first interface for wireless stuff. Should probably be
     * selected in another way. though... */
    for (i = 0; i < MAX_NR_INTERFACES; i++) {
	if (!DEV_NR(i).enabled)
	    continue;
	ifname = DEV_NR(i).ifname;
	break;
    }

    str = strtok(spy_addrs, ",");
    if (index(str, ':'))
	do {
	    struct sockaddr mac_addr;

	    mac_addr.sa_family = ARPHRD_ETHER;
	    if (mac_to_bin(str, mac_addr.sa_data) < 0) {
		fprintf(stderr, "Bad MAC address %s\n", str);
		exit(-1);
	    }
	    if (link_qual_add_spy(&mac_addr, ifname) < 0) {
		fprintf(stderr, "Could not add %s to spy list\n", str);
		exit(-1);
	    }

	} while ((str = strtok(NULL, ",")));
    else
	do {
	    struct sockaddr ip_addr;
	    struct sockaddr_in *sin = (struct sockaddr_in *) &ip_addr;

	    sin->sin_family = AF_INET;
	    sin->sin_port = 0;

	    sin->sin_addr.s_addr = inet_addr(str);

	    if (sin->sin_addr.s_addr == INADDR_NONE) {
		fprintf(stderr, "%s is not a valid spy address\n", str);
		exit(-1);
	    }

	    if (link_qual_add_spy_by_ip(&ip_addr, ifname) < 0) {
		fprintf(stderr, "Could not add %s to spy list\n", str);
		exit(-1);
	    }

	    DEBUG(LOG_DEBUG, 0, "%s added to spy list.",
		  ip_to_str(sin->sin_addr));

	} while ((str = strtok(NULL, ",")));
}

/* Clear spy lists for affected interfaces and close wireless socket... */
void link_qual_cleanup(void)
{
    clear_spy_lists();
    close(iwsock);
}

int ip2mac(struct in_addr ip_addr, struct sockaddr *mac)
{
    int i;

    for (i = 0; i < nipmac; i++) {
	if (memcmp(&ip_addr, &ip2mac_table[i].ip, sizeof(struct in_addr)) == 0) {
	    memcpy(&mac->sa_data, &ip2mac_table[i].mac, ETH_ALEN);
	    mac->sa_family = ARPHRD_ETHER;
	    return 1;
	}
    }
    return -1;
}

char *iw_pr_ether(char *buffer, unsigned char *ptr)
{
    sprintf(buffer, "%02X:%02X:%02X:%02X:%02X:%02X",
	    (ptr[0] & 0xFF), (ptr[1] & 0xFF), (ptr[2] & 0xFF),
	    (ptr[3] & 0xFF), (ptr[4] & 0xFF), (ptr[5] & 0xFF)
	);
    return (buffer);
}

/* Originally from wireless tools: */
static int mac_to_bin(char *bufp, unsigned char *mac)
{
    char c, *orig;
    int i, val;

    i = 0;
    orig = bufp;
    while ((*bufp != '\0') && (i < ETH_ALEN)) {
	val = 0;
	c = *bufp++;
	if (isdigit(c))
	    val = c - '0';
	else if (c >= 'a' && c <= 'f')
	    val = c - 'a' + 10;
	else if (c >= 'A' && c <= 'F')
	    val = c - 'A' + 10;
	else
	    return (-1);

	val <<= 4;
	c = *bufp++;
	if (isdigit(c))
	    val |= c - '0';
	else if (c >= 'a' && c <= 'f')
	    val |= c - 'a' + 10;
	else if (c >= 'A' && c <= 'F')
	    val |= c - 'A' + 10;
	else
	    return (-1);

	*mac++ = (unsigned char) (val & 0377);
	i++;

	if (*bufp == ':') {
	    if (i == ETH_ALEN);	/* nothing */
	    bufp++;
	}
    }

    /* That's it.  Any trailing junk? */
    if ((i == ETH_ALEN) && (*bufp != '\0'))
	return (-1);

    return (0);
}

/* Some stuff from iwspy.c here... (Jean Tourrilhes <jt@hpl.hp.com>) */
static int link_qual_get(struct sockaddr *mac_addr, char *ifname)
{
    int i, n;
    struct iwreq wrq;
    char buffer[(sizeof(struct iw_quality) +
		 sizeof(struct sockaddr)) * IW_MAX_SPY];
    struct sockaddr hwa[IW_MAX_SPY];
    struct iw_quality qual[IW_MAX_SPY];
    /*   char mac1[128], mac2[128]; */

    /* Collect stats */
    wrq.u.data.pointer = (caddr_t) buffer;
    wrq.u.data.length = 0;
    wrq.u.data.flags = 0;
    strncpy(wrq.ifr_name, ifname, IFNAMSIZ);

    if (ioctl(iwsock, SIOCGIWSPY, &wrq) < 0) {
	fprintf(stderr, "Could not get link quality : %s\n", strerror(errno));
	exit(-1);
    }

    /* Number of addresses */
    n = wrq.u.data.length;

    memcpy(hwa, buffer, n * sizeof(struct sockaddr));
    memcpy(qual, buffer + n * sizeof(struct sockaddr),
	   n * sizeof(struct iw_quality));
    for (i = 0; i < n; i++) {
	if (memcmp(&mac_addr->sa_data, &hwa[i].sa_data, ETH_ALEN) == 0)
	    return qual[i].qual;
    }
    return (-1);
}

int link_qual_get_from_ip(struct in_addr ip_addr, char *ifname)
{
    struct sockaddr mac;
    int qual;

    if (ip2mac(ip_addr, &mac) < 0) {
	fprintf(stderr, "ip2mac failed!\n");
	exit(-1);
    }
    qual = link_qual_get(&mac, ifname);

    /*  printf("%s qual=%d\n", ip_to_str(ip_addr), qual); */
    fflush(stdout);

    return qual;
}

/* Taken from iwlib.c in wireless tools which in turn claims to be a
 * cut & paste from net-tools-1.2.0 */
int iw_check_mac_addr_type(int skfd, char *ifname)
{
    struct ifreq ifr;

    /* Get the type of hardware address */
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
    if ((ioctl(skfd, SIOCGIFHWADDR, &ifr) < 0) ||
	(ifr.ifr_hwaddr.sa_family != ARPHRD_ETHER)) {
	/* Deep trouble... */
	fprintf(stderr, "Interface %s doesn't support MAC addresses\n", ifname);
	return (-1);
    }
    return 0;
}

int link_qual_add_spy(struct sockaddr *mac_addr, char *ifname)
{
    int nbr = 0;
    struct iwreq wrq;
    struct sockaddr hw_address[IW_MAX_SPY];
    char buffer[(sizeof(struct iw_quality) +
		 sizeof(struct sockaddr)) * IW_MAX_SPY];
    /* Check sanity of MAC address */
    if (iw_check_mac_addr_type(iwsock, ifname) < 0) {
	fprintf(stderr, "Bad MAC address!\n");
	return (-1);
    }

    wrq.u.data.pointer = (caddr_t) buffer;
    wrq.u.data.length = 0;
    wrq.u.data.flags = 0;

    strncpy(wrq.ifr_name, ifname, IFNAMSIZ);

    if (ioctl(iwsock, SIOCGIWSPY, &wrq) < 0) {
	fprintf(stderr, "Interface doesn't accept reading addresses...\n");
	fprintf(stderr, "%s\n", strerror(errno));
	return (-1);
    }
    nbr = wrq.u.data.length;

    /* Copy old addresses */
    if (nbr > 0)
	memcpy(hw_address, buffer, nbr * sizeof(struct sockaddr));

    /* Add the new address */
    if (nbr >= IW_MAX_SPY) {
	fprintf(stderr, "Spy limit reached!\n");
	return -1;
    }

    memcpy(&hw_address[nbr], mac_addr, sizeof(struct sockaddr));

    nbr++;

    wrq.u.data.pointer = (caddr_t) hw_address;
    wrq.u.data.length = nbr;
    wrq.u.data.flags = 0;

    strncpy(wrq.ifr_name, ifname, IFNAMSIZ);

    if (ioctl(iwsock, SIOCSIWSPY, &wrq) < 0) {
	fprintf(stderr, "SIOCSIWSPY failed!\n");
	return (-1);
    }
    return 0;
}

/* Some code taken from iwlib.c in wireless tools which in turn claims
 * to be a cut & paste from net-tools-1.2.0 */
int link_qual_add_spy_by_ip(struct sockaddr *ip_addr, char *ifname)
{
    struct arpreq arpq;
    struct sockaddr_in *sin = (struct sockaddr_in *) ip_addr;
    memset(&arpq, 0, sizeof(struct arpreq));
    memcpy(&arpq.arp_pa, ip_addr, sizeof(struct sockaddr));

    arpq.arp_pa.sa_family = AF_INET;
    arpq.arp_ha.sa_family = 0;
    arpq.arp_flags = 0;
    strncpy(arpq.arp_dev, ifname, IFNAMSIZ);

    if ((ioctl(iwsock, SIOCGARP, &arpq) < 0) || !(arpq.arp_flags & ATF_COM)) {
	fprintf(stderr,
		"Arp failed for %s, %s : %s\nTry to ping the address before setting it.\n",
		ip_to_str(((struct sockaddr_in *) ip_addr)->sin_addr),
		ifname, strerror(errno));
	return (-1);
    }

    /* Store address pairs */
    if (nipmac > IW_MAX_SPY) {
	fprintf(stderr, "IW_MAX_SPY reached!!\n");
	return (-1);
    }

    memcpy(&ip2mac_table[nipmac].ip, &sin->sin_addr, sizeof(struct in_addr));
    memcpy(ip2mac_table[nipmac].mac, &arpq.arp_ha.sa_data, ETH_ALEN);
    nipmac++;

    if (link_qual_add_spy(&arpq.arp_ha, ifname) < 0) {
	fprintf(stderr, "Set spy failed...\n");
	return (-1);
    }
    return 0;
}
