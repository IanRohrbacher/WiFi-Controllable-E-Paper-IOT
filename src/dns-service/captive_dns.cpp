/**
 * @file captive_dns.cpp
 * 
 * @brief Implementation of a minimal DNS server used for captive portal
 * redirection.
 * 
 * @details 
 * 
 */

#include <DNSServer.h>

#include "captive_dns.h"
#include "configs.h"
#include "logger.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the captive DNS service.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
/** Timestamp for adding debug logs for the DNS loop */
unsigned long nowLoop = 0;
/** DNS server instance */
DNSServer dnsServer;

} // namespace
/** @} */ // end of Private


/**
 * @defgroup Public
 * Public API for the captive DNS service, declared in captive_dns.h.
 * @{
 */
bool const startDNSService(const IPAddress& apIp, uint16_t dnsPort) {
  uint8_t retries = 0;
  while (retries < dns_config::kStartRetries) {
    if (dnsServer.start(dnsPort, "*", apIp)) {
      debug_logs::dnsLogging("Started DNS service on IP: %s, Port: %u", apIp.toString().c_str(), dnsPort);
      return true;
    }
    debug_logs::dnsLogging("Failed to start DNS service on attempt %u. Retrying...", retries + 1);
    retries++;
  }
  debug_logs::dnsLogging("Failed to start DNS service after %u attempts.", dns_config::kStartRetries);
  return false;
}

bool const stopDNSService() {
  try {
    dnsServer.stop();
    debug_logs::dnsLogging("Stopped DNS service.");
    return true;
  } catch (const std::exception& e) {
    debug_logs::dnsLogging("Error stopping DNS service: %s", e.what());
    return false;
  }
}

bool const updateDNSService() {
  if (!dnsServer.isForwarding()) {
    debug_logs::dnsLogging("DNS service is not running.");
    return false;
  }
  try {
    dnsServer.processNextRequest();  
    if (millis() - nowLoop >= debug_config::kDNSLoopDelay) {
      debug_logs::dnsLogging("DNS request processed.");
      nowLoop = millis();
    }
    return true;
  } catch (const std::exception& e) {
    debug_logs::dnsLogging("Error processing DNS request: %s", e.what());
    return false;
  }
}
/** @} */ // end of Public
