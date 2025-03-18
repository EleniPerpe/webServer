/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   webServer.cpp                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: piotr <piotr@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/02/16 21:12:30 by anamieta          #+#    #+#             */
/*   Updated: 2025/03/18 19:46:11 by piotr            ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "webServer.hpp"
#include <map>
#include "SocketManager.hpp"
#include "HTTPRequest.hpp"
#include "HTTPResponse.hpp"
#include "parseConfig.hpp"
#include "FileUtilis.hpp"
#include <vector>
#include <dirent.h>

volatile sig_atomic_t timeoutOccurred = 0;

// ======================================================================
//  1️⃣ Constructor & Initialization
// ======================================================================

webServer::webServer(const std::unordered_multimap<std::string, std::string>& serverConfig,
	const std::unordered_multimap<std::string, std::vector<std::string>>& locationConfig)
: _serverConfig(serverConfig), _locationConfig(locationConfig), _socketManager()
{
std::cout << "[INFO] Initializing web server..." << std::endl;

for (const auto& entry : _serverConfig)
{
if (entry.first == "listen")
{
int port = std::stoi(entry.second);

if (auto portStatus = _socketManager.isPortAvailable(port); portStatus.has_value()) {
std::cerr << "Error: Port " << port << " is already in use. Details: " << *portStatus << std::endl;
continue;
}

try {
_socketManager.createSocket(port);
}
catch (const std::runtime_error& e)
{
std::cerr << "Error: " << e.what() << std::endl;
continue;
}
}
}

if (_socketManager.getServerSockets().empty())
{
std::cerr << "Error: No valid server sockets created" << std::endl;
throw std::runtime_error("No valid server sockets created");
}
}

void webServer::start()
{
    std::cout << "[SUCCESS] Server started on configured ports!" << std::endl;
    while (true)
	{
        int pollCount = poll(_socketManager.getPollFds().data(), _socketManager.getPollFds().size(), 500);  // 500ms timeout
        if (pollCount < 0)
		{
            std::cerr << "Error: Polling failed" << std::endl;
            continue;
        }

        for (int i = _socketManager.getPollFds().size() - 1; i >= 0; --i)
        {
            short revents = _socketManager.getPollFds()[i].revents; // Store revents in a local variable

            if (revents & POLLIN)
			{
                auto it = std::find_if(
                    _socketManager.getServerSockets().begin(),
                    _socketManager.getServerSockets().end(),
                    [fd = _socketManager.getPollFds()[i].fd](const Socket& socket)
					{
                        return socket.getFd() == fd;
                    });

					if (it != _socketManager.getServerSockets().end())
					{
						auto clientSocketOpt = _socketManager.acceptConnection(_socketManager.getPollFds()[i].fd);
						if (clientSocketOpt)
						{
							std::string serverName = it->getServerName();
							addConnection(*clientSocketOpt, serverName);
						}
					}
                else
                {
                    processRead(_socketManager.getPollFds()[i].fd);
                }
            }

            if (revents & POLLOUT)
			{
                processWrite(_socketManager.getPollFds()[i].fd);
            }

            if (revents & (POLLERR | POLLHUP))
			{
                _socketManager.closeSocket(_socketManager.getPollFds()[i].fd);
            }
        }
    }
}

// ======================================================================
//   Connection Management
// ======================================================================

void webServer::addConnection(int clientFd, const std::string& serverName)
{
    Connection conn;
    conn.socket = Socket(clientFd);
    conn.requestComplete = false;
    conn.serverName = serverName;  // Save the server name for later use
    _connections[clientFd] = std::move(conn);
}

void webServer::closeConnection(int clientFd)
{
    _connections.erase(clientFd);
    _socketManager.closeSocket(clientFd);
}

void webServer::updatePollEvents(int fd, short newEvent)
{
    auto it = std::find_if(_socketManager.getPollFds().begin(), _socketManager.getPollFds().end(), 
        [fd](const struct pollfd& pfd) { return pfd.fd == fd; });
    
    if (it != _socketManager.getPollFds().end())
	{
        it->events = newEvent;
    }
}

void webServer::processRead(int clientSocket)
{
    // Verify the connection exists.
    if (_connections.find(clientSocket) == _connections.end())
    {
        std::cerr << "[ERROR] Received data for unknown client: " << clientSocket << std::endl;
        closeConnection(clientSocket);
        return;
    }
	
    std::string fullRequest = readFullRequest(clientSocket);
    if (fullRequest.empty())
    {
        closeConnection(clientSocket);
        return;
    }

    // Get the connection for this client.
    Connection& conn = _connections[clientSocket];
    conn.inputBuffer = fullRequest;

    // If the server name hasn't been set yet, extract it from the Host header.
    if (conn.serverName.empty())
    {
        HTTPRequest req(fullRequest);
        std::string host = req.getHeader("Host");
        conn.serverName = (!host.empty()) ? host : "default";
    }

    // Enforce the client_max_body_size limit.
    size_t maxBodySize = getClientMaxBodySize(conn.serverName);
    if (fullRequest.size() > maxBodySize)
    {
        std::cerr << "[ERROR] Request size (" << fullRequest.size() 
                  << " bytes) exceeds client_max_body_size (" << maxBodySize 
                  << " bytes) for server " << conn.serverName << "\n";
        std::string response = generateErrorResponse(413, "Payload Too Large");
        sendResponse(conn.socket, response);
        closeConnection(clientSocket);
        return;
    }

    // Process the full HTTP request.
    std::string responseStr = handleRequest(fullRequest);
    conn.outputBuffer = responseStr;
    updatePollEvents(clientSocket, POLLOUT);
}

void webServer::processWrite(int clientSocket)
{
    if (_connections.find(clientSocket) == _connections.end())
	{
        std::cerr << "[ERROR] Trying to write to unknown client: " << clientSocket << std::endl;
        closeConnection(clientSocket);
        return;
    }

    Connection &conn = _connections[clientSocket];
    if (!conn.outputBuffer.empty())
    {
        ssize_t bytesWritten = write(conn.socket.getFd(), conn.outputBuffer.c_str(), conn.outputBuffer.size());

        if (bytesWritten > 0)
        {
            conn.outputBuffer.erase(0, bytesWritten);
            // If all data is sent, close the connection
            if (conn.outputBuffer.empty())
            {
                closeConnection(clientSocket);
            }
        }
        else
        {
            std::cerr << "[ERROR] Failed to write to client: " << clientSocket << std::endl;
            closeConnection(clientSocket);
        }
    }
}

// ======================================================================
//   Request & Response Handling
// ======================================================================


std::string webServer::generateResponse(const HTTPRequest& request)
{
    return handleRequest(request.getRawRequest());
}

std::string webServer::readFullRequest(int clientSocket) {
    std::string request;
    char buffer[4096] = {0};
    ssize_t bytesRead;
    
    // Read headers
    while ((bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0)) > 0) {
        if (bytesRead <= 0) {
            std::cerr << "[ERROR] Empty request received from client: " << clientSocket << std::endl;
            return "";
        }
        buffer[bytesRead] = '\0';
        request.append(buffer, bytesRead);

        // Check for end of headers
        size_t headersEnd = request.find("\r\n\r\n");
        if (headersEnd != std::string::npos) {
            break;
        }
    }

    if (request.find("\r\n\r\n") == std::string::npos) {
        std::cerr << "[ERROR] Request headers not properly terminated" << std::endl;
        return "";
    }

    // Parse headers
    std::string headerSection = request.substr(0, request.find("\r\n\r\n"));
    auto headers = parseHeaders(headerSection);

    // Handle "Expect: 100-continue" if present
    auto expectIt = headers.find("Expect");
    if (expectIt != headers.end() && expectIt->second == "100-continue") {
        const char* continueResponse = "HTTP/1.1 100 Continue\r\n\r\n";
        send(clientSocket, continueResponse, strlen(continueResponse), 0);
    }

    // Get content length and read body if needed
    size_t contentLength = getContentLength(headers);
    size_t bodyBytesRead = 0;

    // Read the body
    if (contentLength > 0) {
        size_t headersEndPos = request.find("\r\n\r\n") + 4;
        bodyBytesRead = request.size() - headersEndPos; // Number of bytes already read from body
        while (bodyBytesRead < contentLength) {
            bytesRead = recv(clientSocket, buffer, std::min(sizeof(buffer) - 1, contentLength - bodyBytesRead), 0);
            if (bytesRead < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // No data available right now; you might want to poll again or sleep briefly
                    continue;
                } else {
                    std::cerr << "[ERROR] recv() failed" << std::endl;
                    break;
                }
            } else if (bytesRead == 0) {
                std::cerr << "[ERROR] Connection closed before full body received" << std::endl;
                break;
            }
            buffer[bytesRead] = '\0';
            request.append(buffer, bytesRead);
            bodyBytesRead += bytesRead;
        }
    }

    // Debug the full request
    std::cout << "[DEBUG] Full request: " << request << std::endl;

    // Only access the body if the headers are properly separated and body is present
    size_t bodyStart = request.find("\r\n\r\n");
    if (bodyStart != std::string::npos) {
        std::cout << "[DEBUG] Request body size: " << request.substr(bodyStart + 4).size() << " bytes" << std::endl;
    } else {
        std::cerr << "[ERROR] No body found in the request." << std::endl;
    }

    return request;
}

std::string webServer::handleRequest(const std::string& fullRequest) {
    if (fullRequest.empty())
        return generateErrorResponse(400, "Empty request");

    HTTPRequest httpRequest(fullRequest);
    std::string method = httpRequest.getMethod();
    std::string rawPath = httpRequest.getPath();
    std::string decodedPath = urlDecode(rawPath);

    std::cout << "[DEBUG] Request Path: " << rawPath << "\n";
    std::cout << "[DEBUG] Decoded Path: " << decodedPath << "\n";

    bool methodAllowed = false;
    std::string matchedLocation = "";

    // Find the longest matching location for the path
    for (const auto& [location, allowedMethods] : _allowedMethods) {
        if (decodedPath.find(location) == 0 && location.length() > matchedLocation.length()) {
            matchedLocation = location;
            methodAllowed = (std::find(allowedMethods.begin(), allowedMethods.end(), method) != allowedMethods.end());
        }
    }

    // If no matching location is found, assume the method is allowed (or handle as needed)
    if (matchedLocation.empty()) {
        methodAllowed = true; // Default behavior if no location-specific rules are defined
    }

    // If the method is not allowed, return a 405 Method Not Allowed response
    if (!methodAllowed) {
        std::cout << "[DEBUG] Method " << method << " not allowed for path: " << decodedPath << "\n";
        return generateMethodNotAllowedResponse();
    }
    // Handle explicit redirection codes (e.g. /301 or /302 in the URL)
    if (decodedPath.size() > 4 &&
       (decodedPath.substr(0, 4) == "/301" || decodedPath.substr(0, 4) == "/302")) {
        size_t urlStart = decodedPath.find_first_not_of(" \t", 4);
        if (urlStart != std::string::npos) {
            std::string targetUrl = decodedPath.substr(urlStart);
            if (targetUrl.find("http://") != 0 && targetUrl.find("https://") != 0)
                targetUrl = "http://" + targetUrl;
            std::string statusCode = decodedPath.substr(1, 3);
            std::stringstream response;
            response << "HTTP/1.1 " << statusCode << " Moved\r\n";
            response << "Location: " << targetUrl << "\r\n";
            response << "Content-Length: 0\r\n";
            response << "\r\n";
            return response.str();
        }
    }
	
    for (const auto& [location, redirection] : _redirections) {
        if (rawPath.find(location) == 0 || decodedPath.find(location) == 0) {
            std::cout << "[DEBUG] Redirection match found for location: " << location << "\n";
            size_t spacePos = redirection.find_first_of(" \t");
            if (spacePos != std::string::npos) {
                std::string statusCode = redirection.substr(0, spacePos);
                std::string targetUrl = redirection.substr(spacePos + 1);
                if (targetUrl.find("http://") != 0 && targetUrl.find("https://") != 0)
                    targetUrl = "http://" + targetUrl;
                std::stringstream response;
                response << "HTTP/1.1 " << statusCode << " Moved\r\n";
                response << "Location: " << targetUrl << "\r\n";
                response << "Content-Length: 0\r\n";
                response << "\r\n";
                return response.str();
            } else {
                std::cerr << "[ERROR] Invalid redirection format: " << redirection << std::endl;
            }
        }
    }

    // Handle CGI requests.
    if (rawPath.find("/cgi-bin/") == 0) {
        auto itCgi = _locationConfig.find("/cgi-bin/");
        if (itCgi == _locationConfig.end()) {
            return generateErrorResponse(500, "CGI configuration not found for location: /cgi-bin/");
        }
        std::string cgiPass;
        std::string scriptFilename;
        for (const std::string& directive : itCgi->second) {
            if (directive.find("cgi_pass") == 0) {
                size_t pos = directive.find_first_of(" \t");
                if (pos != std::string::npos)
                    cgiPass = directive.substr(pos + 1);
            } else if (directive.find("cgi_param SCRIPT_FILENAME") == 0) {
                size_t pos = directive.find_first_of(" \t");
                if (pos != std::string::npos)
                    scriptFilename = directive.substr(pos + 1);
            }
        }
        if (cgiPass.empty() || scriptFilename.empty()) {
            return generateErrorResponse(500, "CGI configuration is incomplete for location: /cgi-bin/");
        }
        std::string rootDir = "./www"; // Default root directory
        for (const std::string& directive : itCgi->second) {
            if (directive.find("root") == 0) {
                size_t pos = directive.find_first_of(" \t");
                if (pos != std::string::npos) {
                    rootDir = directive.substr(pos + 1);
                    break;
                }
            }
        }
        std::string queryString;
        size_t queryPos = rawPath.find('?');
        std::string scriptPath = rootDir + rawPath;
        if (queryPos != std::string::npos) {
            queryString = rawPath.substr(queryPos + 1);
            std::string cgiPath = rawPath.substr(0, queryPos);
            scriptPath = rootDir + cgiPath;
        }
        return executeCGI(scriptPath, method, queryString, httpRequest.getBody());
    }

    // Determine the root directory from _serverConfig, defaulting to "./www"
    std::string rootDir = "./www";
    auto itServer = _serverConfig.find("root");
    if (itServer != _serverConfig.end()) {
        rootDir = itServer->second;
    }

    // Handle POST requests (uploads and form submissions).
    if (method == "POST") {
        std::string contentType = httpRequest.getHeader("Content-Type");
        if (contentType.empty())
            contentType = "text/plain";
        std::string serverName = httpRequest.getHeader("Host");
        if (serverName.empty())
            serverName = "default";

        // Special handling for upload paths.
        if (rawPath == "/upload" || rawPath.find("/upload/") == 0) {
            // Check if it's a multipart/form-data request
			if (contentType.find("multipart/form-data") != std::string::npos) {
				// Extract the boundary from the Content-Type header
				size_t boundaryPos = contentType.find("boundary=");
				if (boundaryPos == std::string::npos) {
					return generateErrorResponse(400, "Missing boundary in Content-Type");
				}
				std::string boundary = contentType.substr(boundaryPos + 9);
				boundary = "--" + boundary;
				
				// Get the request body
				std::string body = httpRequest.getBody();
				
				// Check the Content-Length before processing
				std::string contentLengthStr = httpRequest.getHeader("Content-Length");				
				// Process the upload if size check passes
				return generateSuccessResponse("Files uploaded successfully");
			}
        }
        
        // For all other POST requests, delegate to generatePostResponse
        return generatePostResponse(httpRequest.getBody(), contentType, serverName);
    }

    // Resolve the file path for non-POST requests.
    std::string filePath = resolveFilePath(decodedPath, rootDir);
    if (filePath.empty())
        return generateErrorResponse(400, "Invalid path");

    std::cout << "[DEBUG] Resolved File Path: " << filePath << "\n";

    // Check if the resolved path is a directory.
    struct stat st;
    if (stat(filePath.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        std::cout << "[DEBUG] Path is a directory\n";
        
        // Check autoindex configuration.
        bool autoindexEnabled = false;
        std::string matchedLocation = "";
        std::cout << "[DEBUG] Autoindex Configuration:\n";
        for (const auto& [loc, enabled] : _autoindexConfig) {
            std::cout << "  - Location: '" << loc << "', Autoindex: " 
                      << (enabled ? "on" : "off") << "\n";
        }
        for (const auto& locationPair : _autoindexConfig) {
            const std::string& loc = locationPair.first;
            if (!loc.empty() && rawPath.find(loc) == 0) {
                if (loc.length() > matchedLocation.length()) {
                    matchedLocation = loc;
                    autoindexEnabled = locationPair.second;
                }
            }
        }
        std::cout << "[DEBUG] Matched Location: " << matchedLocation 
                  << ", Autoindex: " << (autoindexEnabled ? "on" : "off") << "\n";

        if (autoindexEnabled) {
            return generateDirectoryListing(filePath, decodedPath);
        } else {
            // Attempt to serve index.html.
            std::string indexPath = filePath;
            if (indexPath.back() != '/')
                indexPath += "/";
            indexPath += "index.html";
            if (access(indexPath.c_str(), F_OK) == 0) {
                return generateGetResponse(indexPath);
            } else {
                return generateErrorResponse(403, "Directory listing is disabled, and no index file found.");
            }
        }
    }

    // Handle specific HTTP methods
    if (method == "GET") {
        return generateGetResponse(filePath);
    } else if (method == "DELETE") {
        return generateDeleteResponse(filePath);
    } else {
        // Handle unsupported methods
        return generateMethodNotAllowedResponse();
    }
}

void webServer::sendResponse(Socket& clientSocket, const std::string& response)
{
    ssize_t bytesWritten = write(clientSocket.getFd(), response.c_str(), response.size());
    if (bytesWritten < 0)
	{
        std::cerr << "Error sending response: " << strerror(errno) << std::endl;
    }
}

// ======================================================================
//   Response Generation
// ======================================================================


std::string webServer::generateGetResponse(const std::string& filePath)
{
    std::cout << "[INFO] Handling GET request for: " << filePath << std::endl;

    auto [fileContent, contentType] = FileUtils::readFile(filePath);
    if (fileContent.empty())
    {
        auto [defaultContent, defaultContentType] = HTTPResponse::getDefaultErrorPage(404);
        HTTPResponse errorResponse(404, defaultContentType, defaultContent);
        return errorResponse.generateResponse();
    }

    std::cout << "[INFO] Serving file: " << filePath << " (" 
              << fileContent.size() << " bytes, " << contentType << ")" << std::endl;

    HTTPResponse response(200, contentType, std::string(fileContent.data(), fileContent.size()));
    return response.generateResponse();
}


std::string webServer::generatePostResponse(const std::string& requestBody,
	const std::string& contentType,
	const std::string& serverName)
{
(void)serverName; // to remove

std::cout << "[DEBUG] Processing POST request" << std::endl;
std::cout << "[DEBUG] Content-Type: " << contentType << std::endl;
std::cout << "[DEBUG] Request body size: " << requestBody.size() << " bytes" << std::endl;

// Get upload directory from server configuration
std::string uploadDir = "./www/html/upload";
auto it = _serverConfig.find("upload_dir");
if (it != _serverConfig.end()) {
uploadDir = it->second;
}

// Create upload directory if it doesn't exist
if (!FileUtils::createDirectoryIfNotExists(uploadDir)) {
return generateErrorResponse(500, "Server configuration error");
}

// Handle multipart/form-data (file uploads)
if (contentType.find("multipart/form-data") != std::string::npos) {
return handleMultipartUpload(requestBody, contentType, uploadDir);
}
// Handle application/x-www-form-urlencoded
else if (contentType.find("application/x-www-form-urlencoded") != std::string::npos) {
return handleFormUrlEncodedUpload(requestBody, uploadDir);
}
// Handle text/plain
else if (contentType.find("text/plain") != std::string::npos) {
return handleTextUpload(requestBody, uploadDir);
}
// Unsupported content type
else {
std::cerr << "[ERROR] Unsupported content type: " << contentType << std::endl;
return generateErrorResponse(415, "Unsupported Media Type");
}
}

std::string webServer::generateDeleteResponse(const std::string& filePath)
{
    std::string adjustedFilePath;
    const std::string marker = "/upload/";
    size_t pos = filePath.find(marker);
    
    if (pos != std::string::npos) {
        std::string filename = filePath.substr(pos + marker.length());
        adjustedFilePath = "./www/html/upload/" + filename;
    } else {
        adjustedFilePath = filePath;
    }
    
    if (!FileUtils::deleteFile(adjustedFilePath)) 
	{
        return generateErrorResponse(500, "Failed to delete file");
    }
    
    return "HTTP/1.1 204 No Content\r\n\r\n";
}

std::string webServer::generateErrorResponse(int errorCode, const std::string& errorMessage) {
    // Get the default error page for the given error code
    auto [errorPageContent, contentType] = HTTPResponse::getDefaultErrorPage(errorCode);

    // Generate the HTTP response
    std::string response = "HTTP/1.1 " + std::to_string(errorCode) + " " + errorMessage + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + std::to_string(errorPageContent.size()) + "\r\n";
    response += "\r\n";
    response += errorPageContent;

    return response;
}

std::string webServer::generateSuccessResponse(const std::string& message) {
    std::stringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: text/plain\r\n";
    response << "Content-Length: " << message.size() << "\r\n";
    response << "\r\n";
    response << message;
    return response.str();
}

std::string webServer::generateMethodNotAllowedResponse()
{
    auto [errorContent, errorContentType] = HTTPResponse::getDefaultErrorPage(405);
        
    std::stringstream response;
    response << "HTTP/1.1 405 Method Not Allowed\r\n";
    response << "Content-Type: " << errorContentType << "\r\n";
    response << "Content-Length: " << errorContent.size() << "\r\n";
    response << "Allow: GET, POST, DELETE\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    
    response.write(errorContent.data(), errorContent.size());
    return response.str();
}

std::string webServer::generateDirectoryListing(const std::string& directoryPath, const std::string& requestPath)
{
    DIR* dir = opendir(directoryPath.c_str());
    if (!dir)
    {
        std::cerr << "[ERROR] Failed to open directory: " << directoryPath << "\n";
        return generateErrorResponse(500, "Failed to open directory");
    }

    std::stringstream response;
    response << "<html><head><title>Index of " << requestPath << "</title></head><body>";
    response << "<h1>Index of " << requestPath << "</h1>";
    response << "<ul>";

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        std::string fileName = entry->d_name;
        if (fileName == "." || fileName == "..")
            continue;

        response << "<li><a href=\"" << requestPath << "/" << fileName << "\">" << fileName << "</a></li>";
    }

    response << "</ul></body></html>";
    closedir(dir);

    std::stringstream httpResponse;
    httpResponse << "HTTP/1.1 200 OK\r\n";
    httpResponse << "Content-Type: text/html\r\n";
    httpResponse << "Content-Length: " << response.str().size() << "\r\n";
    httpResponse << "\r\n";
    httpResponse << response.str();

    return httpResponse.str();
}

// ======================================================================
//   CGI Processing
// ======================================================================


void handleTimeout(int signal) {
    if (signal == SIGALRM) {
        timeoutOccurred = 1;
    }
}

std::string webServer::executeCGI(const std::string& scriptPath, const std::string& method, 
    const std::string& queryString, const std::string& requestBody)
{
    // Set up the timeout handler
    signal(SIGALRM, handleTimeout);
    timeoutOccurred = 0;

    // Set a timeout for the CGI script
    alarm(5);

    // Extract location from script path (assuming location is the directory part)
    std::string location = scriptPath.substr(0, scriptPath.rfind('/'));
    if (location.empty()) location = "/";
    
    // Get CGI configuration for this location
    const CGIConfig& cgiConfig = getCGIConfig(location);
    
    // Remove any query string from the script path
    std::string cleanScriptPath = scriptPath.substr(0, scriptPath.find('?'));

    int pipeToChild[2], pipeFromChild[2];
    if (pipe(pipeToChild) == -1 || pipe(pipeFromChild) == -1)
        return generateErrorResponse(500, "Failed to create pipes");

    pid_t pid = fork();
    if (pid == -1)
        return generateErrorResponse(500, "Failed to fork");

    if (pid == 0) { // Child process.
        close(pipeToChild[1]);  // Close unused write end.
        close(pipeFromChild[0]); // Close unused read end.

        if (dup2(pipeToChild[0], STDIN_FILENO) == -1 ||
            dup2(pipeFromChild[1], STDOUT_FILENO) == -1) {
            std::cerr << "Failed to redirect stdin/stdout" << std::endl;
            exit(1);
        }

        // Set CGI environment variables using the configuration
        // Use configured request method or fallback to the provided method
        std::string requestMethodEnv = cgiConfig.requestMethod.empty() ? 
                                     method : cgiConfig.requestMethod;
        // Replace variables if present
        if (requestMethodEnv.find("$request_method") != std::string::npos) {
            requestMethodEnv = method;
        }
        setenv("REQUEST_METHOD", requestMethodEnv.c_str(), 1);

        // Use configured query string or fallback to the provided one
        std::string queryStringEnv = cgiConfig.queryString.empty() ? 
                                   queryString : cgiConfig.queryString;
        // Replace variables if present
        if (queryStringEnv.find("$query_string") != std::string::npos) {
            queryStringEnv = queryString;
        }
        setenv("QUERY_STRING", queryStringEnv.c_str(), 1);

        // Set content length
        setenv("CONTENT_LENGTH", std::to_string(requestBody.size()).c_str(), 1);
        
        // Determine content type from request or use a reasonable default
        // Ideally this would come from the HTTP request headers
        std::string contentType = "application/x-www-form-urlencoded"; // Default, but should be passed from request
        setenv("CONTENT_TYPE", contentType.c_str(), 1);
        
        // Use script filename from config or default to the clean script path
        std::string scriptFilename = cgiConfig.scriptFilename.empty() ? 
                                     cleanScriptPath : cgiConfig.scriptFilename;
        // Replace any variables in the template
        size_t pos;
        if ((pos = scriptFilename.find("$fastcgi_script_name")) != std::string::npos) {
            std::string scriptName = cleanScriptPath.substr(cleanScriptPath.rfind('/') + 1);
            scriptFilename.replace(pos, 20, scriptName);
        }
        setenv("SCRIPT_FILENAME", scriptFilename.c_str(), 1);
        
        // Similar replacement for PATH_INFO
        std::string pathInfo = cgiConfig.pathInfo.empty() ?
                               cleanScriptPath : cgiConfig.pathInfo;
        if ((pos = pathInfo.find("$fastcgi_script_name")) != std::string::npos) {
            std::string scriptName = cleanScriptPath.substr(cleanScriptPath.rfind('/') + 1);
            pathInfo.replace(pos, 20, scriptName);
        }
        setenv("PATH_INFO", pathInfo.c_str(), 1);

        // Use the configured CGI interpreter path, or use a server-wide default
        // This should be a configurable default instead of hardcoded
        std::string interpreter = cgiConfig.cgiPass;
        if (interpreter.empty()) {
            // Look for a server-wide default in the server configuration
            auto it = _serverConfig.find("default_cgi_interpreter");
            if (it != _serverConfig.end()) {
                interpreter = it->second;
            } else {
                // Last resort fallback
                interpreter = "/usr/bin/python3"; 
            }
        }
                                 
        // Extract the interpreter name from the path
        std::string interpreterName = interpreter.substr(interpreter.rfind('/') + 1);
        
        execlp(interpreter.c_str(), interpreterName.c_str(), cleanScriptPath.c_str(), nullptr);

        // If execlp returns, an error occurred.
        std::cerr << "Failed to execute CGI script: " << cleanScriptPath 
                  << " (Error: " << strerror(errno) << ")" << std::endl;
        exit(1);
    }

    // Parent process
    close(pipeToChild[0]);
    close(pipeFromChild[1]);

    if (method == "POST" && !requestBody.empty())
        write(pipeToChild[1], requestBody.c_str(), requestBody.size());
    close(pipeToChild[1]);

    std::string cgiOutput;
    char buffer[4096];
    fd_set readSet;
    struct timeval timeout;

    // Set a timeout for reading from the pipe (e.g., 5 seconds)
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    while (true) {
        FD_ZERO(&readSet);
        FD_SET(pipeFromChild[0], &readSet);

        int selectResult = select(pipeFromChild[0] + 1, &readSet, nullptr, nullptr, &timeout);
        if (selectResult < 0) {
            std::cerr << "select() error" << std::endl;
            break;
        } else if (selectResult == 0) {
            // Timeout occurred while waiting for data
            std::cerr << "Timeout while reading CGI output" << std::endl;
            break;
        }

        if (FD_ISSET(pipeFromChild[0], &readSet)) {
            ssize_t bytesRead = read(pipeFromChild[0], buffer, sizeof(buffer));
            if (bytesRead > 0)
                cgiOutput.append(buffer, bytesRead);
            else if (bytesRead == 0)
                break; // EOF.
            else {
                std::cerr << "read() error" << std::endl;
                break;
            }
        }
    }
    close(pipeFromChild[0]);

    // Check if a timeout occurred
    if (timeoutOccurred) {
        std::cerr << "CGI script timed out" << std::endl;
        kill(pid, SIGKILL); // Terminate the child process
        waitpid(pid, nullptr, 0); // Clean up the child process
        return generateErrorResponse(504, "Gateway Timeout");
    }

    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        // Check if the CGI script already provided HTTP headers
        if (cgiOutput.find("HTTP/1.") != 0) {
            // Use configured HTTP version or default to 1.1
            std::string httpVersion = "HTTP/1.1";
            auto it = _serverConfig.find("http_version");
            if (it != _serverConfig.end()) {
                httpVersion = it->second;
            }
            
            std::string headers = httpVersion + " 200 OK\r\n";
            
            // If there are no headers in the CGI output, add some defaults
            if (cgiOutput.find("\r\n\r\n") == std::string::npos) {
                // Look for a default content type in server config or use text/html
                std::string defaultContentType = "text/html";
                auto it = _serverConfig.find("default_cgi_content_type");
                if (it != _serverConfig.end()) {
                    defaultContentType = it->second;
                }
                
                headers += "Content-Type: " + defaultContentType + "\r\n";
                headers += "Content-Length: " + std::to_string(cgiOutput.size()) + "\r\n\r\n";
            }
            cgiOutput = headers + cgiOutput;
        }
        return cgiOutput;
    }
    return generateErrorResponse(500, "CGI script failed");
}

// ======================================================================
//   Server Configuration & Settings
// ======================================================================

void webServer::setRootDirectories(const std::map<std::string, std::string>& rootDirectories) {
    _rootDirectories = rootDirectories;
}

void webServer::setCGIConfig(const std::map<std::string, webServer::CGIConfig>& cgiConfig) {
    _cgiConfig = cgiConfig;
}

const webServer::CGIConfig& webServer::getCGIConfig(const std::string& location) const {
    auto it = _cgiConfig.find(location);
    if (it != _cgiConfig.end()) {
        return it->second;
    }
    static const webServer::CGIConfig defaultConfig;
    return defaultConfig;
}

std::vector<std::string> webServer::getAllowedMethods(const std::string& location) const {
    auto it = _allowedMethods.find(location);
    if (it != _allowedMethods.end()) {
        return it->second;
    }
    return {}; // Return an empty vector if no methods are specified
}

void webServer::setAllowedMethods(const std::map<std::string, std::vector<std::string>>& allowedMethods) {
    _allowedMethods = allowedMethods;
}

void webServer::setAutoindexConfig(const std::map<std::string, bool>& autoindexConfig)
{
    _autoindexConfig = autoindexConfig;
}
void webServer::setRedirections(const std::map<std::string, std::string>& redirections)
{
    _redirections = redirections;
}
void webServer::setServerNames(const std::map<std::string, std::string>& serverNames)
{
    _serverNames = serverNames; // Assign the parameter to the member variable
}
void webServer::setClientMaxBodySize(const std::string& serverName, size_t size) {
    _clientMaxBodySizes[serverName] = size;
    std::cout << "[DEBUG] Set client_max_body_size for server '" << serverName 
              << "' to " << size << " bytes\n";
}

size_t webServer::getClientMaxBodySize(const std::string& serverName) const {
    // Try exact match first
    auto it = _clientMaxBodySizes.find(serverName);
    if (it != _clientMaxBodySizes.end()) {
        std::cout << "[DEBUG] Found client_max_body_size for server '" << serverName 
                 << "': " << it->second << " bytes\n";
        return it->second;
    }
    
    // Try without port number
    size_t colonPos = serverName.find(":");
    if (colonPos != std::string::npos) {
        std::string nameWithoutPort = serverName.substr(0, colonPos);
        it = _clientMaxBodySizes.find(nameWithoutPort);
        if (it != _clientMaxBodySizes.end()) {
            std::cout << "[DEBUG] Found client_max_body_size for server '" << nameWithoutPort 
                     << "': " << it->second << " bytes\n";
            return it->second;
        }
    }
    
    // Try just "localhost" as fallback
    it = _clientMaxBodySizes.find("localhost");
    if (it != _clientMaxBodySizes.end()) {
        std::cout << "[DEBUG] Found client_max_body_size for 'localhost': " 
                 << it->second << " bytes\n";
        return it->second;
    }
    
    std::cout << "[DEBUG] Using default client_max_body_size for server '" << serverName 
             << "': 1048576 bytes\n";
    return 1048576;
}

size_t webServer::getContentLength(const std::unordered_map<std::string, std::string>& headers)
{
    auto it = headers.find("Content-Length");
    if (it != headers.end()) {
        try {
            return std::stoul(it->second);
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Invalid Content-Length value: " << e.what() << std::endl;
        }
    }
    return 0;
}

// ======================================================================
//   Request Process & Helpers
// ======================================================================

std::string webServer::getStatusMessage(int statusCode)
{
    switch (statusCode)
    {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        default:  return "Unknown Status";
    }
}

std::unordered_map<std::string, std::string> webServer::parseHeaders(const std::string& headerSection)
{
    std::unordered_map<std::string, std::string> headerMap;
    std::istringstream stream(headerSection);
    std::string line;

    while (std::getline(stream, line) && line != "\r")
    {
        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos)
        {
            std::string name = line.substr(0, colonPos);
            std::string value = line.substr(colonPos + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of("\r\n") + 1);
            headerMap[name] = value;
        }
    }
    return headerMap;
}

std::string webServer::resolveFilePath(const std::string& path, const std::string& rootDir)
{
    std::string resolvedPath = sanitizePath(path);
    if (resolvedPath.empty() || resolvedPath[0] != '/')
        return "";
    if (resolvedPath.find("/cgi-bin/") == 0)
        return rootDir + resolvedPath;
    
    // Debug: Print out all entries in _rootDirectories
    std::cout << "[DEBUG] _rootDirectories contents:\n";
    for (const auto& [loc, root] : _rootDirectories) {
        std::cout << "  Location: '" << loc << "', Root: '" << root << "'\n";
    }
    
    // Check for location-specific root directory from _rootDirectories
    std::string matchedLocation = "";
    std::string locationRootDir = rootDir; // Default to server root
    for (const auto& [location, customRoot] : _rootDirectories) {
        std::cout << "[DEBUG] Checking location '" << location << "' against path '" << resolvedPath << "'\n";
        if (!location.empty() && resolvedPath.find(location) == 0) {
            std::cout << "[DEBUG] Match found!\n";
            if (location.length() > matchedLocation.length()) {
                matchedLocation = location;
                locationRootDir = customRoot;
                std::cout << "[DEBUG] Using location '" << matchedLocation << "' with root '" << locationRootDir << "'\n";
            }
        }
    }
    
    if (!matchedLocation.empty()) {
        std::string relativePath = resolvedPath.substr(matchedLocation.size());
        if (relativePath.empty() || relativePath[0] != '/')
            relativePath = "/" + relativePath;
        std::cout << "[DEBUG] Final path: '" << locationRootDir + relativePath << "'\n";
        return locationRootDir + relativePath;
    }
    
    // Check for alias mapping
    for (const auto& [locationPrefix, aliasDir] : _aliasDirectories) {
        if (resolvedPath.compare(0, locationPrefix.size(), locationPrefix) == 0) {
            std::string relativePath = resolvedPath.substr(locationPrefix.size());
            return aliasDir + relativePath;
        }
    }
    std::cout << "[DEBUG] No special location found, using default: '" << rootDir + resolvedPath << "'\n";
    return rootDir + resolvedPath;
}

std::string urlDecode(const std::string& encoded)
{
    std::string result;
    for (size_t i = 0; i < encoded.length(); ++i)
	{
        if (encoded[i] == '%' && i + 2 < encoded.length())
		{
            int value;
            std::istringstream is(encoded.substr(i + 1, 2));
            if (is >> std::hex >> value)
			{
                result += static_cast<char>(value);
                i += 2;
            }
			else
			{
                result += encoded[i];
            }
        } 
		else if (encoded[i] == '+') 
		{
            result += ' ';
        } 
		else 
		{
            result += encoded[i];
        }
    }
    return result;
}

std::string webServer::handleMultipartUpload(const std::string& requestBody,
	const std::string& contentType,
	const std::string& uploadDir)
{
std::cout << "[DEBUG] Processing multipart/form-data upload" << std::endl;

// Extract boundary from Content-Type header
std::string boundary;
size_t boundaryPos = contentType.find("boundary=");
if (boundaryPos == std::string::npos) {
std::cerr << "[ERROR] No boundary found in Content-Type" << std::endl;
return generateErrorResponse(400, "Missing boundary in multipart/form-data");
}

boundary = contentType.substr(boundaryPos + 9);
if (boundary.front() == '"' && boundary.back() == '"') {
boundary = boundary.substr(1, boundary.size() - 2);
}
std::cout << "[DEBUG] Boundary: " << boundary << std::endl;

// Full boundary string
std::string fullBoundary = "--" + boundary;

// Find the first boundary
size_t boundaryStart = requestBody.find(fullBoundary);
if (boundaryStart == std::string::npos) {
std::cerr << "[ERROR] Could not find boundary in request body" << std::endl;
return generateErrorResponse(400, "Malformed multipart/form-data");
}

// Find the headers section
size_t headersStart = boundaryStart + fullBoundary.length();
if (headersStart >= requestBody.size()) {
std::cerr << "[ERROR] Headers section not found" << std::endl;
return generateErrorResponse(400, "Malformed multipart/form-data");
}

// Skip CRLF after boundary
if (requestBody[headersStart] == '\r' && headersStart + 1 < requestBody.size() && requestBody[headersStart + 1] == '\n') {
headersStart += 2;
}

// Find the end of headers
size_t headersEnd = requestBody.find("\r\n\r\n", headersStart);
if (headersEnd == std::string::npos) {
std::cerr << "[ERROR] End of headers not found" << std::endl;
return generateErrorResponse(400, "Malformed multipart/form-data");
}

// Extract headers
std::string headers = requestBody.substr(headersStart, headersEnd - headersStart);
std::cout << "[DEBUG] Headers: " << headers << std::endl;

// Extract filename
std::string filename = "uploaded_file_" + getCurrentTimeString() + ".bin";
size_t filenamePos = headers.find("filename=\"");
if (filenamePos != std::string::npos) {
size_t start = filenamePos + 10;
size_t end = headers.find("\"", start);
if (end != std::string::npos) {
filename = sanitizeFilename(headers.substr(start, end - start));
std::cout << "[DEBUG] Filename: " << filename << std::endl;
}
}

// Content starts after headers
size_t contentStart = headersEnd + 4; // Skip double CRLF

// Find the next boundary
size_t nextBoundary = requestBody.find(fullBoundary, contentStart);
if (nextBoundary == std::string::npos) {
std::cerr << "[ERROR] Closing boundary not found" << std::endl;
return generateErrorResponse(400, "Malformed multipart/form-data");
}

// Content ends before next boundary
size_t contentEnd = nextBoundary;
if (contentEnd > 2 && requestBody[contentEnd - 2] == '\r' && requestBody[contentEnd - 1] == '\n') {
contentEnd -= 2;
}

// Extract content
std::string content = requestBody.substr(contentStart, contentEnd - contentStart);
std::cout << "[DEBUG] Content size: " << content.size() << " bytes" << std::endl;

// Save file
std::string filePath = uploadDir + "/" + filename;
std::cout << "[DEBUG] Saving file to: " << filePath << std::endl;

if (!FileUtils::writeFile(filePath, content)) {
return generateErrorResponse(500, "Failed to save file");
}

std::cout << "[POST] File saved: " << filename << std::endl;

// Return success response
std::string responseText = "File uploaded successfully: " + filename;
return HTTPResponse(201, "text/plain", responseText).generateResponse();
}

std::string webServer::handleFormUrlEncodedUpload(const std::string& requestBody,
	const std::string& uploadDir)
{
std::cout << "[DEBUG] Processing form-urlencoded upload" << std::endl;

std::string filename = "form_data_" + getCurrentTimeString() + ".txt";
std::string filePath = uploadDir + "/" + filename;

std::cout << "[DEBUG] Saving form data to: " << filePath << std::endl;

if (!FileUtils::writeFile(filePath, requestBody)) {
return generateErrorResponse(500, "Failed to save file");
}

std::cout << "[POST] Form data saved: " << filename << std::endl;

// Return success response
std::string responseText = "Form data uploaded successfully: " + filename;
return HTTPResponse(201, "text/plain", responseText).generateResponse();
}

std::string webServer::getFilePath(const std::string& path)
{
    std::string basePath = "./www/html";
    if (path == "/" || path.empty())
	{
        return basePath + "/index.html";
    }
    return basePath + path;
}

std::string webServer::handleTextUpload(const std::string& requestBody,
	const std::string& uploadDir)
{
std::cout << "[DEBUG] Processing plain text upload" << std::endl;

// Parse headers to extract filename (if available)
std::unordered_map<std::string, std::string> headers = parseHeaders(requestBody);
std::string filename = "text_data_" + getCurrentTimeString() + ".txt";

if (headers.find("Content-Disposition") != headers.end()) {
std::string disposition = headers["Content-Disposition"];
size_t filenamePos = disposition.find("filename=\"");
if (filenamePos != std::string::npos) {
size_t start = filenamePos + 10;
size_t end = disposition.find("\"", start);
if (end != std::string::npos) {
filename = sanitizeFilename(disposition.substr(start, end - start));
std::cout << "[DEBUG] Original filename extracted: " << filename << std::endl;
}
}
}

// Save file
std::string filePath = uploadDir + "/" + filename;
std::cout << "[DEBUG] Saving text data to: " << filePath << std::endl;

if (!FileUtils::writeFile(filePath, requestBody)) {
return generateErrorResponse(500, "Failed to save file");
}

std::cout << "[POST] Text data saved: " << filename << std::endl;

// Return success response
std::string responseText = "Text data uploaded successfully: " + filename;
return HTTPResponse(201, "text/plain", responseText).generateResponse();
}

