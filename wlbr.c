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
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>


#define USAGE "Usage:\twlbr -c config-file\n\twlbr [-d] wireless-if client-if\n"
#define BUFFER_SIZE 2048
#define MAX_ARGS 6


struct config {
  bool daemonize;
  const char *wirelessInterfaceName;
  const char *clientInterfaceName;
};


void setupAddress(struct sockaddr_ll *address, const int index);


void handler(const int signalNumber);
void getConfig(struct config *config, const int argc, char *const argv[]);
void vwriteLog(const int priority, const char *format, va_list vargs);
void writeLog(const int priority, const char *format, ...);
void exitMessage(const int errrorNumber, const int exitCode,
  const char *format, ...);
void exitUsageError();


int socketFd;


int
main(const int argc, char *const argv[]) {
  struct config config;
  int wirelessInterfaceIndex, clientInterfaceIndex, bytesRead;
  struct sockaddr_ll wirelessInterfaceAddress, clientInterfaceAddress,
    packetAddress;
  uint8_t buffer[BUFFER_SIZE];
  socklen_t packetAddressLen = sizeof(packetAddress);
  struct ifreq ifreq;

  openlog("wlbr", LOG_PID | LOG_NDELAY, LOG_USER);

  memset(&config, 0, sizeof(struct config));
  getConfig(&config, argc, argv);
  if(!(config.wirelessInterfaceName && config.clientInterfaceName))
    exitUsageError();

  /* Check interface names and get indices */
  if(!(wirelessInterfaceIndex = if_nametoindex(config.wirelessInterfaceName)))
    exitMessage(errno, EX_CONFIG,
      "Error: Interface %s does not exist", config.wirelessInterfaceName);
  if(!(clientInterfaceIndex = if_nametoindex(config.clientInterfaceName)))
    exitMessage(errno, EX_CONFIG,
      "Error: Interface %s does not exist", config.clientInterfaceName);

  writeLog(LOG_INFO, "Opening packet socket\n");
  if((socketFd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1)
    exitMessage(errno, EX_IOERR, "Error: Could not create socket");

  if(signal(SIGINT, handler) == SIG_ERR)
    exitMessage(0, EX_OSERR, "Error: Could not register signal handler");
  if(signal(SIGTERM, handler) == SIG_ERR)
    exitMessage(0, EX_OSERR, "Error: Could not register signal handler");

  writeLog(LOG_INFO, "Putting client interface %s into promicuous mode\n",
    config.clientInterfaceName);
  strncpy(ifreq.ifr_name, config.clientInterfaceName, IFNAMSIZ - 1);
  if(ioctl(socketFd, SIOCGIFFLAGS, &ifreq))
    exitMessage(errno, EX_OSERR, "Error: Could not execute interface request");
  ifreq.ifr_flags |= IFF_PROMISC;
  if(ioctl(socketFd, SIOCSIFFLAGS, &ifreq))
    exitMessage(errno, EX_OSERR, "Error: Could not execute interface request");

  /* Set outgoing interface indices and address length */
  memset(&wirelessInterfaceAddress, 0, sizeof(struct sockaddr_ll));
  wirelessInterfaceAddress.sll_ifindex = wirelessInterfaceIndex;
  wirelessInterfaceAddress.sll_halen = ETH_ALEN;
  memset(&clientInterfaceAddress, 0, sizeof(struct sockaddr_ll));
  clientInterfaceAddress.sll_ifindex = clientInterfaceIndex;
  clientInterfaceAddress.sll_halen = ETH_ALEN;
  
  writeLog(LOG_INFO,
"Bridge between wireless interface %s and wired client interface %s now running\n",
    config.wirelessInterfaceName,
    config.clientInterfaceName);
  
  writeLog(LOG_INFO, "Daemonizing process\n");
  if(config.daemonize) {
    if(daemon(0, 0) == -1)
      exitMessage(errno, EX_OSERR, "Error: Could not daemonize process");
  }

  /* Repeater loop */
  while((bytesRead =
      recvfrom(socketFd, buffer, BUFFER_SIZE, 0,
         (struct sockaddr*) &packetAddress, &packetAddressLen)))
    if(packetAddress.sll_pkttype != 4) {
      if(packetAddress.sll_ifindex == wirelessInterfaceIndex)
        if(sendto(socketFd, buffer, bytesRead, 0,
              (struct sockaddr*) &clientInterfaceAddress,
              sizeof(struct sockaddr_ll)) !=
            bytesRead)
          exitMessage(errno, EX_IOERR, "Error: Could not send data");
      if(packetAddress.sll_ifindex == clientInterfaceIndex)
        if(sendto(socketFd, buffer, bytesRead, 0,
              (struct sockaddr*) &wirelessInterfaceAddress,
              sizeof(struct sockaddr_ll)) !=
            bytesRead)
          exitMessage(errno, EX_IOERR, "Error: Could not send data");
    }

  return EX_OK;
}


void
handler(const int signalNumber) {
  (void) signalNumber;
  writeLog(LOG_INFO, "Closing bridge and packet socket\n");
  close(socketFd);
  closelog();
  exit(EX_OK);
}

void
getConfig(struct config *config, const int argc, char *const argv[]) {
  char *configFileArgv[MAX_ARGS + 1];
  char buffer[BUFFER_SIZE];
  int file, bytesRead, nonoption = 0, optChar, configFileArgc = 0;

  while((optChar = getopt(argc, argv, "-c:d")) != -1) {
    if(optChar == 1) {
      switch(nonoption) {
        case 0:
          config->wirelessInterfaceName = optarg;
          break;
        case 1:
          config->clientInterfaceName = optarg;
          break;
        default:
          exitUsageError();
      }
      nonoption++;
    }
    else {
      if(nonoption) exitMessage(0, EX_USAGE, USAGE);
      switch(optChar) {
        case 'c':
          if(!*argv) exitMessage(0, EX_USAGE, USAGE);
          if(argc != 3) exitMessage(0, EX_USAGE, USAGE);
          if((file = open(optarg, O_RDONLY)) == -1)
            exitMessage(errno, EX_IOERR, "Error: Could not open input file");
          if((bytesRead = read(file, buffer, BUFFER_SIZE - 1)) == -1)
            exitMessage(errno, EX_IOERR, "Error: Could not read from file");
          buffer[bytesRead] = 0;
          bytesRead = read(file, buffer, BUFFER_SIZE - 1);
          if(bytesRead == -1)
            exitMessage(errno, EX_IOERR, "Error: Could not read from file");
          if(bytesRead != 0)
            exitMessage(0, EX_CONFIG, "Error: Config file too large");
          close(file);
          strtok(buffer, "\n");
          configFileArgv[configFileArgc] = NULL;
          if((configFileArgv[++configFileArgc] = strtok(buffer, " ")))
            do if(configFileArgc == MAX_ARGS)
              exitMessage(0, EX_CONFIG, "Error: Too many arguments");
            while ((configFileArgv[++configFileArgc] = strtok(NULL, " ")));
          configFileArgv[configFileArgc] = NULL;
          optind = 0;
          getConfig(config, configFileArgc, configFileArgv);
          return;
          break;
        case 'd':
          config->daemonize = true;
          break;
        default:
          exitUsageError();
      }
    }
  }
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

void
exitUsageError() {
  fprintf(stderr, USAGE);
  syslog(LOG_ERR, "Error: Invalid options. Please see usage");
  exit(EX_USAGE);
}
