/**
 * @file dns/captive_dns.h
 * @brief Simple DNS service wrapper used by the captive portal.
 *
 * The DNS service listens on the configured port and redirects most host
 * lookups to the AP IP so browsers will contact the device's HTTP server.
 */

#pragma once

#include <ESP8266WebServer.h>
#include <IPAddress.h>
#include <WString.h>

/** Start the DNS service to respond to client queries. */
void startDNSService(const IPAddress& apIp, uint16_t dnsPort);

/** Stop the DNS service. */
void stopDNSService();

/** Poll the DNS service; call regularly from the main loop. */
void updateDNSService();
