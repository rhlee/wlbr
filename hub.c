#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>


#define USAGE "Usage: hub [-d] network-if client-if\n"
#define BUFFER_SIZE 2048


void setupAddress(struct sockaddr_ll *address, const int index);


void exitMessage(const int exitCode,  const char *format, ...);

/* TODO: Close fd's */

int
main(const int argc, const char **argv) {
  const char *networkInterfaceName, *clientInterfaceName;
  int socketFd, networkInterfaceIndex, clientInterfaceIndex, bytesRead;
  struct sockaddr_ll networkInterfaceAddress, clientInterfaceAddress, packetAddress;
  uint8_t buffer[BUFFER_SIZE];
  socklen_t packetAddressLen = sizeof(packetAddress);
  struct ifreq ifreq;
  
  if(!((argc == 3) || ((argc == 4) && strncmp(argv[1], "-d", 3)))) {
    printf(USAGE);
    exit(1);
  }
  networkInterfaceName = argv[argc - 2];
  clientInterfaceName = argv[argc - 1];

  /* Check interface names and get indices */
  if(!(networkInterfaceIndex = if_nametoindex(networkInterfaceName)))
    exitMessage(1, "Error: Interface %s does not exist", networkInterfaceName);
  if(!(clientInterfaceIndex = if_nametoindex(clientInterfaceName)))
    exitMessage(1, "Error: Interface %s does not exist", clientInterfaceName);

  /* Create socket */
  if((socketFd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1)
    exitMessage(1, "Error: Could not create socket");

  /* Put client interface into promiscuous mode */
  strncpy(ifreq.ifr_name, clientInterfaceName, IFNAMSIZ - 1);
  if(ioctl(socketFd, SIOCGIFFLAGS, &ifreq))
    exitMessage(1, "Error: Could not execute interface reuqest");
  ifreq.ifr_flags |= IFF_PROMISC;
  if(ioctl(socketFd, SIOCSIFFLAGS, &ifreq))
    exitMessage(1, "Error: Could not execute interface reuqest");

  /* Set outgoing interface indices and address length */
  memset(&networkInterfaceAddress, 0, sizeof(struct sockaddr_ll));
  networkInterfaceAddress.sll_ifindex = networkInterfaceIndex;
  networkInterfaceAddress.sll_halen = ETH_ALEN;
  memset(&clientInterfaceAddress, 0, sizeof(struct sockaddr_ll));
  clientInterfaceAddress.sll_ifindex = clientInterfaceIndex;
  clientInterfaceAddress.sll_halen = ETH_ALEN;
  
  /* Repeater loop */
  while((bytesRead =
      recvfrom(socketFd, buffer, BUFFER_SIZE, 0,
         (struct sockaddr*) &packetAddress, &packetAddressLen)))
    if(packetAddress.sll_pkttype != 4) {
      if(packetAddress.sll_ifindex == networkInterfaceIndex)
        if(sendto(socketFd, buffer, bytesRead, 0,
              (struct sockaddr*) &clientInterfaceAddress, sizeof(struct sockaddr_ll)) !=
            bytesRead)
          exitMessage(1, "Error: Could not send data");
      if(packetAddress.sll_ifindex == clientInterfaceIndex)
        if(sendto(socketFd, buffer, bytesRead, 0,
              (struct sockaddr*) &networkInterfaceAddress, sizeof(struct sockaddr_ll)) !=
            bytesRead)
          exitMessage(1, "Error: Could not send data");
    }

  return 0;
}


void
exitMessage(const int exitCode,  const char *format, ...) {
  va_list args;

  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fprintf(stderr, "\nErrno: %d, %s\n", errno, strerror(errno));
  
  exit(exitCode);
}
