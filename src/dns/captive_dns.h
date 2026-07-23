/**
 * @headerfile captive_dns.h "src/dns/captive_dns.h"
 *
 */

#pragma once

/**
 * @brief Start the DNS module to respond to client queries.
 *
 * @details
 * Retries @c dnsServer.start() up to @c dns_config::kDNSInitAttempts times
 * before giving up. Every query on @p dnsPort is answered with @p apIp
 * regardless of hostname, which only makes sense for a captive portal.
 *
 * @param apIp The IP address of the access point to which clients will connect.
 * @param dnsPort The port on which the DNS module will listen.
 *
 * @return The status of the DNS module startup attempt.
 * @retval true The DNS module was started successfully.
 * @retval false @c dnsServer.start() failed on every attempt.
 *
 */
bool startDNSModule(const IPAddress& apIp, uint16_t dnsPort);

/**
 * @brief Stop the DNS module.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void stopDNSModule();

/**
 * @brief Process incoming DNS requests.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void updateDNSModule();

/**
 * @brief Start the mDNS responder so the device is reachable at @c http://{dns_config::kPortalHost}.local/.
 *
 * @details
 * Also advertises an "http" service on @c wifi_config::kWebPort, so mDNS
 * browsers (e.g. @c avahi-browse) can discover the portal.
 *
 * @warning Must be called after the WiFi AP is up.
 *
 * @par Parameters
 * None.
 *
 * @return The status of the mDNS responder startup attempt.
 * @retval true The mDNS responder was started successfully.
 * @retval false The mDNS responder failed to start.
 * 
 */
bool startMDNSModule();

/**
 * @brief Process incoming mDNS queries.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void updateMDNSModule();
