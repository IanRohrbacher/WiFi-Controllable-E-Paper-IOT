/**
 * @file captive_dns.h
 * @brief 
 *
 */

#pragma once

/** Start the DNS service to respond to client queries. */
void startDNSService(const IPAddress& apIp, uint16_t dnsPort);

/** Stop the DNS service. */
void stopDNSService();

/** Poll the DNS service; call regularly from the main loop. */
bool updateDNSService();
