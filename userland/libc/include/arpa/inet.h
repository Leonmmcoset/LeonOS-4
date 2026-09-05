#ifndef LEONOS_ARPA_INET_H
#define LEONOS_ARPA_INET_H

#include <netinet/in.h>

uint16_t htons(uint16_t value);
uint16_t ntohs(uint16_t value);
uint32_t htonl(uint32_t value);
uint32_t ntohl(uint32_t value);
in_addr_t inet_addr(const char *text);
int inet_pton(int family, const char *source, void *destination);

#endif
