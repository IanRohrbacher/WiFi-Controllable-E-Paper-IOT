/**
 * @headerfile website.h "src/website-service/website.h"
 * 
 */

#pragma once

#include <ESP8266WebServer.h>

/**
 * @brief Start the web server to serve the captive portal and API endpoints.
 * 
 * @details
 * This function initializes the web server, mounts the LittleFS filesystem,
 * and registers the necessary routes for the captive portal and API endpoints.
 * 
 * @param server Reference to the ESP8266WebServer instance to initialize.
 * 
 * @return The status of the web server startup attempt.
 * @retval true The web server was started successfully.
 * @retval false The web server failed to start, possibly due to a filesystem issue or other error.
 * 
 */
bool startWebService(ESP8266WebServer& server);
