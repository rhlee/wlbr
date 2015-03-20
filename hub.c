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


#define USAGE "Usage: hub [-d] ifname0 ifname1\n"
#define BUFFER_SIZE 2048


void setPromiscuous(const int socketFd, const char *interfaceName);
void setupAddress(struct sockaddr_ll *address, const int index);


void exitMessage(const int exitCode,  const char *format, ...);

// TODO: Close fd's

int
main(const int argc, const char **argv) {
  const char *interface0Name, *interface1Name;
  int socketFd, interface0Index, interface1Index, bytesRead;
  struct sockaddr_ll interface0Address, interface1Address, packetAddress;
  uint8_t buffer[BUFFER_SIZE];
  socklen_t packetAddressLen = sizeof(packetAddress);
  
  if(!((argc == 3) || ((argc == 4) && strncmp(argv[1], "-d", 3)))) {
    printf(USAGE);
    exit(1);
  }
  interface0Name = argv[argc - 2];
  interface1Name = argv[argc - 1];

  // Check interface names and get indices
  if(!(interface0Index = if_nametoindex(interface0Name)))
    exitMessage(1, "Error: Interface %s does not exist", interface0Name);
  if(!(interface1Index = if_nametoindex(interface1Name)))
    exitMessage(1, "Error: Interface %s does not exist", interface1Name);

  // Create socket
  if((socketFd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1)
    exitMessage(1, "Error: Could not create socket");

  setPromiscuous(socketFd, interface0Name);
  setPromiscuous(socketFd, interface1Name);

  setupAddress(&interface0Address, interface0Index);
  setupAddress(&interface1Address, interface1Index);
  
  // Repeater loop
  while((bytesRead =
      recvfrom(socketFd, buffer, BUFFER_SIZE, 0,
         (struct sockaddr*) &packetAddress, &packetAddressLen)))
    if(packetAddress.sll_pkttype != 4) {
      if(packetAddress.sll_ifindex == interface0Index)
        if(sendto(socketFd, buffer, bytesRead, 0,
              (struct sockaddr*) &interface1Address, sizeof(struct sockaddr_ll)) !=
            bytesRead)
          exitMessage(1, "Error: Could not send data");
      if(packetAddress.sll_ifindex == interface1Index)
        if(sendto(socketFd, buffer, bytesRead, 0,
              (struct sockaddr*) &interface0Address, sizeof(struct sockaddr_ll)) !=
            bytesRead)
          exitMessage(1, "Error: Could not send data");
    }

  return 0;
}


void
setPromiscuous(const int socketFd, const char *interfaceName) {
  struct ifreq ifreq;

  strncpy(ifreq.ifr_name, interfaceName, IFNAMSIZ - 1);
  if(ioctl(socketFd, SIOCGIFFLAGS, &ifreq))
    exitMessage(1, "Error: Could not execute interface reuqest");
  ifreq.ifr_flags |= IFF_PROMISC;
  if(ioctl(socketFd, SIOCSIFFLAGS, &ifreq))
    exitMessage(1, "Error: Could not execute interface reuqest");
}

void
setupAddress(struct sockaddr_ll *address, const int index) {
  memset(address, 0, sizeof(struct sockaddr_ll));
  address->sll_ifindex = index;
  address->sll_halen = ETH_ALEN;
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
