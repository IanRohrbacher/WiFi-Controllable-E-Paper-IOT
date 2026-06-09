/**
 * @file dns/captive_dns.cpp
 * @brief Implementation of a minimal DNS server used for captive portal
 * redirection.
 */

#include <DNSServer.h>

#include "captive_dns.h"
#include "configs.h"
#include "logger.h"

namespace {
DNSServer dnsServer;

}  // namespace

// -----------------------------------------------------------------------------
// Public API (declared in captive_dns.h)
// -----------------------------------------------------------------------------

void startDNSService(const IPAddress& apIp, uint16_t dnsPort) {
  dnsServer.start(dnsPort, "*", apIp);
}

void stopDNSService() {
  dnsServer.stop();
}

bool updateDNSService() {
  dnsServer.processNextRequest();
  debug_logs::dnsLogging("DNS request processed.");
  return true;
}

