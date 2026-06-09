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
unsigned long nowLoop = millis();
DNSServer dnsServer;

}  // namespace

// -----------------------------------------------------------------------------
// Public API (declared in captive_dns.h)
// -----------------------------------------------------------------------------

void startDNSService(const IPAddress& apIp, uint16_t dnsPort) {
  dnsServer.start(dnsPort, "*", apIp);
  debug_logs::dnsLogging("Started DNS service on IP: %s, Port: %u", apIp.toString().c_str(), dnsPort);
}

void stopDNSService() {
  dnsServer.stop();
  debug_logs::dnsLogging("Stopped DNS service.");
}

bool updateDNSService() {
  dnsServer.processNextRequest();

  if (millis() - nowLoop >= debug_config::kDNSLoopDelay) {
    debug_logs::dnsLogging("DNS request processed.");
    nowLoop = millis();
  }
  return true;
}

