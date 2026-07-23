/**
 * @headerfile website.h "src/website/website.h"
 * 
 */

#pragma once

#include <ESP8266WebServer.h>

/**
 * @brief Start the web server to serve the captive portal and API endpoints.
 * 
 * @details
 * Retries @c LittleFS.begin() up to @c web_config::kLittleFSRemountAttempts
 * times before giving up, then registers routes (see @c registerRoutes()) and,
 * if @c web_config::kEnablePortal is set, the captive-portal probe redirects
 * (see @c setupPortalEndpoints()).
 *
 * @param server Reference to the ESP8266WebServer instance to initialize.
 *
 * @return The status of the web server startup attempt.
 * @retval true The web server was started successfully.
 * @retval false @c LittleFS.begin() failed on every attempt.
 * 
 */
bool startWebModule(ESP8266WebServer& server);
