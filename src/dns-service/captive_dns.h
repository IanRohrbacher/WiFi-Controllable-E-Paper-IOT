/**
 * @headerfile captive_dns.h "src/dns-service/captive_dns.h"
 * 
 */

#pragma once

/** 
 * @brief Start the DNS service to respond to client queries.
 * 
 * @details 
 * This function initializes the DNS server to listen for incoming DNS
 * requests on the specified port and respond with the provided IP address.
 * This is typically used in captive portal scenarios to redirect clients to
 * a specific IP address when they attempt to access the internet.
 * 
 * @param apIp The IP address of the access point to which clients will connect.
 * @param dnsPort The port on which the DNS service will listen.
 * 
 * @return The status of the DNS service startup attempt.
 * @retval true The DNS service was started successfully.
 * @retval false The DNS service failed to start, possibly due to a port conflict or other issue.
 * 
 */
bool startDNSService(const IPAddress& apIp, uint16_t dnsPort);

/** 
 * @brief Stop the DNS service.
 * 
 * @details
 * This function stops the DNS server from responding to client queries. It
 * should be called when the DNS service is no longer needed, such as when
 * shutting down the captive portal or when the device is being reset.
 * 
 * @par Parameters
 * None.
 * 
 * @par Return
 * Nothing.
 * 
 */
void stopDNSService();

/** 
 * @brief Process incoming DNS requests.
 * 
 * @details
 * This function should be called regularly (e.g., in the main loop) to allow
 * the DNS server to process incoming requests from clients. It checks for
 * new DNS queries and responds according to the configuration set when the
 * service was started.
 * 
 * @par Parameters
 * None.
 * 
 * @par Returns
 * Nothing.
 * 
 */
void updateDNSService();
