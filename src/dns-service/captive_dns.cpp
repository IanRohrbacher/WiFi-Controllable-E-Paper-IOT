/**
 * @file dns/captive_dns.cpp
 * @brief Implementation of a minimal DNS server used for captive portal
 * redirection.
 */

#include "captive_dns.h"

#include <DNSServer.h>

namespace {
DNSServer dnsServer;

}  // namespace

// -----------------------------------------------------------------------------
// Public API (declared in h/dns/captive_dns.h)
// -----------------------------------------------------------------------------

void startDNSService(const IPAddress& apIp, uint16_t dnsPort) {
  dnsServer.start(dnsPort, "*", apIp);
}

void stopDNSService() {
  dnsServer.stop();
}

bool updateDNSService() {
  dnsServer.processNextRequest();
  return true;
}

