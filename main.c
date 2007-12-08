/*****************************************************************************
 *
 * Copyright (C) 2001 Uppsala University and Ericsson Telecom AB.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors: Erik Nordström, <erno3431@student.uu.se>
 *          Henrik Lundgren, <henrikl@docs.uu.se>
 *
 *****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <linux/sockios.h>
#include <linux/wireless.h>

#include "defs.h"
#include "debug.h"
#include "timer_queue.h"
#include "params.h"
#include "aodv_socket.h"
#include "aodv_timeout.h"
#include "k_route.h" 
#include "routing_table.h"
#include "aodv_hello.h"
#include "packet_input.h"

/* Global variables: */
int log_to_file = 0;
int log_rt_table = 0;
int rt_log_interval = 3000; /* msecs between routing table logging */
int unidir_hack = 0;
int rreq_gratuitous = 0;
int use_expanding_ring_search = 1;
int internet_gw_mode = 0;
int use_local_repair = 0;
int receive_n_hellos = 0;
char *progname;
char versionstring[100];

static void cleanup();

void print_usage() {
  printf("AODV-UU v%s, AODV draft v10 © Uppsala University & Ericsson Telecom AB.\n"
	 "Author: Erik Nordström, erno3431@student.uu.se\n\n"
	 "Usage: %s [-i <interface>] [-l] [-n [N]] [-t [<secs>]] [-d] [-g] [-u] [-w]\n\n"
	 "-i  - Network interface to attach to. Defaults to first wireless interface.\n"
	 "-l  - Write all debug output to /var/log/aodvd.log.\n"
	 "-n  - Receive N hellos from host before treating as neighbor.\n"
	 "-t  - Write AODV internal routing table to /var/log/aodvd_rt.log.\n"
	 "-d  - Daemonize, i.e. detach from the console. No output to STDOUT.\n"
	 "-g  - Force the gratuitous flag to be set on all RREQ's.\n"
	 "-u  - Detect and avoid uni-directional links (experimental).\n"
	 "-w  - Enable experimental Internet gateway support.\n\n",
	 AODV_UU_VERSION, progname);
}

int set_kernel_options(char *ifname) {
  int fd = -1;
  char on = '1';
  char off = '0';
  char command[64];
  
  if ((fd = open("/proc/sys/net/ipv4/ip_forward", O_WRONLY)) < 0) 
    return -1;
  if (write(fd, &on, sizeof(char)) < 0)
    return -1;
  close(fd);

  if ((fd = open("/proc/sys/net/ipv4/route/max_delay", O_WRONLY)) < 0) 
    return -1;
  if (write(fd, &off, sizeof(char)) < 0)
    return -1;
  close(fd);

  if ((fd = open("/proc/sys/net/ipv4/route/min_delay", O_WRONLY)) < 0) 
    return -1;
  if (write(fd, &off, sizeof(char)) < 0)
    return -1;
  close(fd);

  /* Disable ICMP redirects: */
  memset(command, '\0', 64);
  sprintf(command, "/proc/sys/net/ipv4/conf/%s/send_redirects", ifname);
  if ((fd = open(command, O_WRONLY)) < 0) 
    return -1;
  if (write(fd, &off, sizeof(char)) < 0)
    return -1;
  close(fd);
  
  memset(command, '\0', 64);
  sprintf(command, "/proc/sys/net/ipv4/conf/%s/accept_redirects", ifname);
  if ((fd = open(command, O_WRONLY)) < 0) 
    return -1;
  if (write(fd, &off, sizeof(char)) < 0)
    return -1;
  close(fd);
  
  return 0;
}

int find_default_gw() {
  FILE *route;
  char buf[100], *l;
  
  route = fopen("/proc/net/route", "r");
  
  if(route == NULL) {
    perror("open /proc/net/route");
    exit(-1);
  }
  
  while(fgets(buf, sizeof(buf), route)) {
    l = strtok(buf, " \t");
    l = strtok(NULL, " \t");
    if(l != NULL) {
      if(strcmp("00000000", l) == 0) {
	l = strtok(NULL, " \t");
	l = strtok(NULL, " \t");
	if(strcmp("0003", l) == 0) {
	  fclose(route);
	  return 1;
	}
      }
    }
  }
  fclose(route);
  return 0;
}
/*
 * Returns the network address of a network interface given its name...
 */
struct sockaddr_in *get_if_info(char *ifname, int type) {
  int skfd;
  struct sockaddr_in *ina;
  struct ifreq ifr;

  /* Get address of interface... */
  skfd=socket(AF_INET, SOCK_DGRAM, 0);

  strcpy(ifr.ifr_name, ifname);
  if (ioctl(skfd, type, &ifr) < 0) {
    log(LOG_ERR, errno, "Could not get address of %s ", ifname);
    close(skfd);
    return NULL;
  }
  else {
    ina = (struct sockaddr_in *)&ifr.ifr_addr;
    close(skfd);
    return ina;
  }
}
/* This will limit the number of handler functions we can have for
   sockets and file descriptors and so on... */
#define CALLBACK_FUNCS 4
static struct callback {
  int fd;
  callback_func_t func;
} callbacks[CALLBACK_FUNCS];

static int nr_callbacks = 0;

int attach_callback_func(int fd, callback_func_t func) {
  if (nr_callbacks >= CALLBACK_FUNCS) {
    fprintf(stderr, "callback attach limit reached!!\n");
    exit(-1);
  }  
  callbacks[nr_callbacks].fd = fd;
  callbacks[nr_callbacks].func = func;
  nr_callbacks++;
  return 0;
}

/* Here we find out how to load the kernel modules... If the modules
   are located in the current directory. use those. Otherwise fall
   back to modprobe. */

void load_modules(char *ifname) {
  struct stat st;
  char buf[1024], *l = NULL;
  int found = 0;
  FILE *m;

  system("/sbin/modprobe iptable_filter &>/dev/null");
  
  memset(buf, '\0', 64);
  if(stat("./ip_queue_aodv.o", &st) < 0)
    sprintf(buf, "/sbin/modprobe ip_queue_aodv &>/dev/null");
  else
    sprintf(buf, "/sbin/insmod ip_queue_aodv.o &>/dev/null");
  system(buf);

  memset(buf, '\0', 64);
  if(stat("./kaodv.o", &st) < 0)
    sprintf(buf, "/sbin/modprobe kaodv ifname=%s &>/dev/null", ifname);
  else
    sprintf(buf, "/sbin/insmod kaodv.o ifname=%s &>/dev/null", ifname);
  system(buf);

  /* Check result */
  m = fopen("/proc/modules", "r");
  while(fgets(buf, sizeof(buf), m)) {
    l = strtok(buf, " \t");
    if(!strcmp(l, "kaodv"))
      found++;
    if(!strcmp(l, "ip_queue_aodv"))
      found++;
  }  
  fclose(m);
  if(found != 2) {
    fprintf(stderr, "A kernel module could not be loaded, check your installation...\n");
    exit(-1);
  }
}

void remove_modules() {
   system("/sbin/modprobe -r kaodv &>/dev/null");
   system("/sbin/modprobe -r ip_queue_aodv &>/dev/null");
   system("/sbin/modprobe -r iptable_filter &>/dev/null");
}

void host_init(char *ifname) {
  static struct local_host_info host_info;
  struct sockaddr_in *ina;
  char buf[1024];
  struct ifconf ifc;
  struct ifreq *ifr;
  int i, iw_sock;

  /* Make sure the global "this_host" pointer points to the info
     structure so that the information can be accessed from
     outside this function... */
  memset(&host_info, '\0', sizeof(struct local_host_info));
  this_host = &host_info;
  
  /* Find the first wireless interface */
  if(ifname != NULL) 
    strcpy(host_info.ifname, ifname);
  else {
    iw_sock= socket(AF_INET, SOCK_DGRAM, 0);
    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;
    if(ioctl(iw_sock, SIOCGIFCONF, &ifc) < 0) {
      fprintf(stderr, "Could not get wireless info\n");
      exit(-1);
    }
    ifr = ifc.ifc_req;
    for(i = ifc.ifc_len / sizeof(struct ifreq); i >= 0; i--, ifr++) {
      struct iwreq req;
	strcpy(req.ifr_name, ifr->ifr_name);
	if (ioctl(iw_sock, SIOCGIWNAME, &req) >= 0) {
	  strcpy(host_info.ifname, ifr->ifr_name);
	  break;
	}
    }
    close(iw_sock);
    
    /* Did we find a wireless interface? */
    if(host_info.ifname[0] == '\0') {
      fprintf(stderr, "Could not find a wireless interface!\n");
      fprintf(stderr, "Use -i <interface> to override...\n");
      exit(-1);
    }
  }
  
  /* Load required kernel modules */
  load_modules(host_info.ifname);
  
  /* Get IP-address of interface... */
  ina = get_if_info(host_info.ifname, SIOCGIFADDR);
  if(ina == NULL)
    exit(-1);
  
  /* Remember our host IP address... */
  host_info.ipaddr = ntohl(ina->sin_addr.s_addr);

  /* printf("Address of interface %s is %s\n", host_info.ifname,  */
  /* 	 ip_to_str(host_info.ipaddr)); */

  /* Get netmask of interface... */
  ina = get_if_info(host_info.ifname, SIOCGIFNETMASK);
  if(ina == NULL)
    exit(-1);
  
  /* Remember the netmask */
  host_info.netmask = ntohl(ina->sin_addr.s_addr);

  /* printf("Netmask of interface %s is %s\n", host_info.ifname,  */
  /* 	 ip_to_str(host_info.netmask)); */
  
  ina = get_if_info(host_info.ifname, SIOCGIFBRDADDR);
  if(ina == NULL)
    exit(-1);

  host_info.broadcast = ntohl(ina->sin_addr.s_addr);
  /* printf("Broadcast address is %s\n", ip_to_str(host_info.broadcast)); */

  /* Enable IP forwarding and set other kernel options... */
  if(set_kernel_options(host_info.ifname) < 0) {
    fprintf(stderr, "Could not set kernel options!\n");
    exit(-1);
  }
  if(internet_gw_mode) {
    if(find_default_gw()) {
      log(LOG_NOTICE, 0, "INIT: Internet gateway mode enabled!");
      host_info.gateway_mode = 1;
    } else {
      host_info.gateway_mode = 0;
      sprintf(buf, "/sbin/route add default gw %s dev %s", 
	      ip_to_str(host_info.ipaddr), host_info.ifname);
      system(buf);
     /*  k_add_rte(0, host_info.ipaddr, 0, 0);  */
      
    }
  }
  /* Add broadcast route (255.255.255.255) */
  k_add_rte(AODV_BROADCAST, 0, 0, 0); 
  
  /* Intitialize the local sequence number an flood_id to zero */
  host_info.seqno = 0;
  host_info.rreq_id = 0;
 
}
/* This signal handler ensures clean exits */
void signal_handler(int type) {

  switch(type) {
  case SIGSEGV:
    log(LOG_ERR, 0, "SEGMENTATION FAULT!!!! Exiting!!! "
	"To get a core dump, compile with DEBUG option.");
  case SIGINT:
  case SIGHUP:
  case SIGTERM:
  default:
    exit(0);
  }
}

int main (int argc, char **argv) {
  static char *ifname = NULL; /* Name of interface to attach to */
  fd_set rfds, readers;
  int n, nfds = 0, i;
  int daemonize = 0;

  /* Remember the name of the executable... */
  progname = strrchr(argv[0], '/');
  
  if (progname)
    progname++;
  else
    progname = argv[0];

  /* Use debug output as default */
  debug = 1;

  /* Parse command line: */
  argc--; argv++;
  while(argc) {
    
    if(argv[0][0] == '-') {
      
      switch(argv[0][1]) {
      case 'i':
	if(argv[1] != NULL) {
	  ifname = argv[1];
	  argc--; argv++;
	  break;
	}
	print_usage();
	exit(-1);
      case 'd':
	debug = 0;
	daemonize = 1;
	break;
      case 'w':
	internet_gw_mode = 1;
	break;
      case 'l':
	log_to_file = 1;
	break;
      case 'n':
	if(argv[1] != NULL && argv[1][0] != '-') {
	  receive_n_hellos = atoi(argv[1]);
	  argc--; argv++;
	} else
	  receive_n_hellos = 3;
	break;
      case 'u':
	unidir_hack = 1;
	break;
      case 'g':
	rreq_gratuitous = 1;
	break;
      case 't':
	log_rt_table = 1;
	if(argv[1] != NULL && argv[1][0] != '-') {
	  rt_log_interval = atoi(argv[1])*1000;
	  argc--; argv++;
	}
	break;
      default:
	print_usage();
	exit(-1);
      }
    } else {
      fprintf(stderr, "Unknown option: %s\n", argv[0]);
      print_usage();
      exit(-1);
    }
    argc--; argv++;
  }
  
  /* Check that we are running as root */
  if (geteuid() != 0) {
    fprintf(stderr, "aodvd: must be root\n");
    exit(1);
  }
  
  /* Detach from terminal */
  if(daemonize) {
    if (fork() != 0) exit(0);
    /* Close stdin, stdout and stderr... */
    close(0);
    close(1);
    close(2);
    setsid();
  }

  /* Initialize data structures and services... */
  host_init(ifname);
  log_init();  
  timer_queue_init();
  rt_table_init();
  packet_input_init();
  aodv_socket_init();
  
  log(LOG_NOTICE, 0,  "INIT: Attaching to %s, override with -i <interface>.", 
      this_host->ifname);

  /* Make sure we cleanup at exit... */
  atexit((void *)&cleanup);

 
  /* Make sure we run at high priority to make up for the user space
     packet processing... */
  /* nice(-5);  */
  
  /* Catch SIGHUP, SIGINT and SIGTERM type signals */
  signal(SIGHUP, signal_handler);
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  /* Only capture segmentation faults when we are not debugging... */
#ifndef DEBUG
  signal(SIGSEGV, signal_handler);
#endif
  /* Set sockets to watch... */
  FD_ZERO(&readers);
  for (i = 0; i < nr_callbacks; i++) {
    FD_SET(callbacks[i].fd, &readers);
    if (callbacks[i].fd >= nfds)
      nfds = callbacks[i].fd + 1;
  }
  
  /* Set bcast_time, which ensures that we don't send unnecessary hello
     msgs. */
  this_host->bcast_time = get_currtime();
 
  /* Set the wait on boot timer... */
  this_host->wait_on_reboot_timer_id = 
    timer_new(DELETE_PERIOD, wait_on_reboot_timeout, this_host);

  log(LOG_NOTICE, 0, "INIT: In wait on reboot for %d milliseconds.", 
      DELETE_PERIOD);
  /* Schedule the first Hello */
  timer_new(HELLO_INTERVAL, hello_send, NULL);
  
  if(log_rt_table)
    timer_new(rt_log_interval, print_rt_table, NULL);

  while(1) {
    memcpy((char *)&rfds, (char *)&readers, sizeof(rfds));
   
    if ((n = select(nfds, &rfds, NULL, NULL, NULL)) < 0) {
      if (errno != EINTR) 
	log(LOG_WARNING, errno, "main.c: Failed select (main loop)");
      continue;
    }

    if (n > 0) {
      for (i = 0; i < nr_callbacks; i++) {
	if (FD_ISSET(callbacks[i].fd, &rfds)) {
	  /* We don't want any timer SIGALRM's while executing the
             callback functions, therefore we block the timer... */
	  timer_block(); 
	  (*callbacks[i].func)(callbacks[i].fd);
	  timer_unblock();
	}
      }
    }
  } /* Main loop */
  return 0;
}

static void cleanup() {
  log(LOG_DEBUG, 0, "CLEANING UP!");
  k_del_rte(AODV_BROADCAST, 0, 0);
  if(internet_gw_mode && !this_host->gateway_mode)
    system("/sbin/route del default");
  remove_modules();
  rt_table_destroy();
  packet_input_cleanup();
  aodv_socket_cleanup();
  log_cleanup();
}
