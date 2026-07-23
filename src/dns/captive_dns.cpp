/**
 * @file captive_dns.cpp
 *
 * @brief Implementation of the captive DNS server and mDNS responder.
 *
 * @details
 * @c startDNSModule() wraps the @c DNSServer library to answer every query
 * with the AP's own IP, while @c startMDNSModule() runs a separate mDNS
 * responder via @c ESP8266mDNS so the portal is also reachable at a fixed
 * hostname.
 *
 */

#include <DNSServer.h>
#include <ESP8266mDNS.h>

#include "captive_dns.h"
#include "configs.h"
#include "logger.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the captive DNS module.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
/** @brief Timestamp for adding debug logs for the DNS loop */
unsigned long nowLoop = 0;
/** @brief Timestamp for adding debug logs for the mDNS loop */
unsigned long mdnsNowLoop = 0;
/** @brief @c DNSServer instance */
DNSServer dnsServer;

} // namespace
/** @} */ // end of Private


/**
 * @defgroup Public
 * Public API for the captive DNS module, declared in captive_dns.h.
 * @{
 */
bool startDNSModule(const IPAddress& apIp, uint16_t dnsPort) {
  uint8_t attempts = 0;
  while (attempts < dns_config::kDNSInitAttempts) {
    if (dnsServer.start(dnsPort, "*", apIp)) {
      debug_logs::dnsLogging("Started DNS module on IP: %s, Port: %u", apIp.toString().c_str(), dnsPort);
      return true;
    }
    debug_logs::dnsLogging("Failed to start DNS module on attempt %u. Retrying...", ++attempts);
  }
  debug_logs::dnsLogging("Failed to start DNS module after %u attempts.", dns_config::kDNSInitAttempts);
  return false;
}

void stopDNSModule() {
  dnsServer.stop();
  debug_logs::dnsLogging("Stopped DNS module.");
}

void updateDNSModule() {
  dnsServer.processNextRequest();
  if (millis() - nowLoop >= debug_config::kDNSLoopDelay) {
    debug_logs::dnsLogging("DNS request processed.");
    nowLoop = millis();
  }
}

bool startMDNSModule() {
  if (!MDNS.begin(dns_config::kPortalHost)) {
    debug_logs::dnsLogging("Failed to start mDNS responder.");
    return false;
  }
  MDNS.addService("http", "tcp", wifi_config::kWebPort);
  debug_logs::dnsLogging("Started mDNS responder: http://%s.local/", dns_config::kPortalHost);
  return true;
}

void updateMDNSModule() {
  MDNS.update();
  if (millis() - mdnsNowLoop >= debug_config::kDNSLoopDelay) {
    debug_logs::dnsLogging("mDNS queries processed.");
    mdnsNowLoop = millis();
  }
}
/** @} */ // end of Public
