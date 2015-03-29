#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sysexits.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>


#define USAGE "Usage:\twlbr -c config-file\n\twlbr [-d] wireless-if client-if"
#define BUFFER_SIZE 2048


void setupAddress(struct sockaddr_ll *address, const int index);


void handler(const int signalNumber);
void vwriteLog(const int priority, const char *format, va_list vargs);
void writeLog(const int priority, const char *format, ...);
void exitMessage(const int errrorNumber, const int exitCode,
  const char *format, ...);


int socketFd;


int
main(const int argc, char *const argv[]) {
  bool nonoption = false;
  const char *configFile = NULL,
    *networkInterfaceName = NULL, *clientInterfaceName = NULL;
  int optChar, networkInterfaceIndex, clientInterfaceIndex, bytesRead;
  struct sockaddr_ll networkInterfaceAddress, clientInterfaceAddress,
    packetAddress;
  uint8_t buffer[BUFFER_SIZE];
  socklen_t packetAddressLen = sizeof(packetAddress);
  struct ifreq ifreq;

  if(signal(SIGINT, handler) == SIG_ERR)
    exitMessage(0, EX_OSERR, "Error: Could not register signal handler");
  if(signal(SIGTERM, handler) == SIG_ERR)
    exitMessage(0, EX_OSERR, "Error: Could not register signal handler");

  while((optChar = getopt(argc, argv, "-c:d:")) != -1) {
    if(optChar == 1) {
      nonoption = true;
      if(networkInterfaceName)
        clientInterfaceName = optarg;
      else
        networkInterfaceName = optarg;
    }
    else {
      if(nonoption) exitMessage(0, EX_USAGE, USAGE);
      switch(optChar) {
        case 'c':
          configFile = optarg;
          break;
        case 'd':
          break;
        default:
          exitMessage(0, EX_USAGE, USAGE);
      }
    }
  }
  /*remove this*/
  (void)configFile;
  
  /* Check interface names and get indices */
  if(!(networkInterfaceIndex = if_nametoindex(networkInterfaceName)))
    exitMessage(errno, EX_CONFIG,
      "Error: Interface %s does not exist",networkInterfaceName);
  if(!(clientInterfaceIndex = if_nametoindex(clientInterfaceName)))
    exitMessage(errno, EX_CONFIG,
      "Error: Interface %s does not exist", clientInterfaceName);

  /* Create socket */
  if((socketFd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1)
    exitMessage(errno, EX_IOERR, "Error: Could not create socket");

  /* Put client interface into promiscuous mode */
  strncpy(ifreq.ifr_name, clientInterfaceName, IFNAMSIZ - 1);
  if(ioctl(socketFd, SIOCGIFFLAGS, &ifreq))
    exitMessage(errno, EX_OSERR, "Error: Could not execute interface reuqest");
  ifreq.ifr_flags |= IFF_PROMISC;
  if(ioctl(socketFd, SIOCSIFFLAGS, &ifreq))
    exitMessage(errno, EX_OSERR, "Error: Could not execute interface reuqest");

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
              (struct sockaddr*) &clientInterfaceAddress,
              sizeof(struct sockaddr_ll)) !=
            bytesRead)
          exitMessage(errno, EX_IOERR, "Error: Could not send data");
      if(packetAddress.sll_ifindex == clientInterfaceIndex)
        if(sendto(socketFd, buffer, bytesRead, 0,
              (struct sockaddr*) &networkInterfaceAddress,
              sizeof(struct sockaddr_ll)) !=
            bytesRead)
          exitMessage(errno, EX_IOERR, "Error: Could not send data");
    }

  return EX_OK;
}


void
handler(const int signalNumber) {
  (void) signalNumber;
  close(socketFd);
  exit(EX_OK);
}

void
vwriteLog(const int priority, const char *format, va_list vargs) {
  va_list vargsCopy;
  FILE *stream = (priority < LOG_ERR ? stdout : stderr);

  va_copy(vargsCopy, vargs);
  vfprintf(stream, format, vargs);
  vsyslog(priority, format, vargsCopy);
  va_end(vargsCopy);
}

void
writeLog(const int priority, const char *format, ...) {
  va_list vargs;

  va_start(vargs, format);
  vwriteLog(priority, format, vargs);
  va_end(vargs);
}

void
exitMessage(const int errorNumber, const int exitCode,
    const char *format, ...) {
  va_list vargs;
  const int priority = (exitCode ? LOG_ERR : LOG_INFO);

  va_start(vargs, format);
  vwriteLog(priority, format, vargs);
  va_end(vargs);
  fprintf((exitCode ? stderr : stdout), "\n");
  if(errorNumber) writeLog(LOG_ERR, "Errno: %d, %s\n", errno, strerror(errno));
  
  exit(exitCode);
}
