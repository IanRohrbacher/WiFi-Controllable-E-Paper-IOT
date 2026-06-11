/**
 * @headerfile captive_dns.h "src/dns-service/captive_dns.h"
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
 * @example
 * @code{.cpp}
 *   IPAddress apIp(192, 168, 4, 1);
 *   startDNSService(apIp, 53);
 * @endcode
 * 
 */
bool startDNSService(const IPAddress& apIp, uint16_t dnsPort);

/** 
 * @brief Stop the DNS service.
 * 
 * @details
 * This function stops the DNS server from responding to client queries. It
 * should be called when the DNS service is no longer needed, such as when
 * shutting down the captive portal or when the device is being reset. The
 * function attempts to stop the DNS server and logs the outcome.
 * 
 * @par Parameters
 * None.
 * 
 * @return The status of the DNS service shutdown attempt.
 * @retval true The DNS service was stopped successfully.
 * @retval false An error occurred while attempting to stop the DNS service.
 * 
 * @example
 * @code{.cpp}
 *  stopDNSService();
 * @endcode
 */
bool stopDNSService();

/** 
 * @brief Process incoming DNS requests.
 * 
 * @details
 * This function should be called regularly (e.g., in the main loop) to allow
 * the DNS server to process incoming requests from clients. It checks for
 * new DNS queries and responds according to the configuration set when the
 * service was started. If the DNS service is not running, it will log a
 * warning message and return false.
 * 
 * @par Parameters
 * None.
 * 
 * @return The status of the DNS service update attempt.
 * @retval true The DNS service is running and processed any pending requests.
 * @retval false The DNS service is not running or an error occurred while processing requests.
 */
bool updateDNSService();
