/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   webServer.cpp                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: piotr <piotr@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/02/16 21:12:30 by anamieta          #+#    #+#             */
/*   Updated: 2025/03/06 20:24:19 by piotr            ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "webServer.hpp"
#include <map>

std::optional<std::string> isPortAvailable(int port)
{
    std::array<char, 128> buffer;
    std::string result;
    std::string command = "lsof -i :" + std::to_string(port);

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        std::cerr << "Failed to run command: " << command << std::endl;
        return "Failed to run command";
    }

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
    {
        result += buffer.data();
    }

    pclose(pipe);

    std::cout << "Port " << port << " check result: " << (result.empty() ? "Available" : "In use") << std::endl;
    if (!result.empty())
    {
        std::cout << "lsof output: " << result << std::endl;
        return result;
    }
    return std::nullopt;
}

webServer::webServer(const std::unordered_multimap<std::string, std::string>& serverConfig,
    const std::unordered_multimap<std::string, std::vector<std::string>>& locationConfig)
: _serverConfig(serverConfig), _locationConfig(locationConfig)
{
std::cout << "[INFO] Initializing web server..." << std::endl;

for (const auto& entry : _serverConfig)
{
if (entry.first == "listen")
{
int port = std::stoi(entry.second);

if (auto portStatus = isPortAvailable(port); portStatus.has_value())
{
    std::cerr << "Error: Port " << port << " is already in use. Details: " << *portStatus << std::endl;
    continue;
}

try {
Socket serverSocket(AF_INET, SOCK_STREAM, 0);
setNonBlocking(serverSocket.getFd());

sockaddr_in serverAddr;
memset(&serverAddr, 0, sizeof(serverAddr));
serverAddr.sin_family = AF_INET;
serverAddr.sin_addr.s_addr = INADDR_ANY;
serverAddr.sin_port = htons(port);

if (bind(serverSocket.getFd(), (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
{
   std::cerr << "Error: Could not bind socket for port " << port << std::endl;
   continue;
}

if (listen(serverSocket.getFd(), 10) < 0)
{
   std::cerr << "Error: Could not listen on socket for port " << port << std::endl;
   continue;
}

_serverSockets.push_back(std::move(serverSocket));

struct pollfd pfd;
pfd.fd = _serverSockets.back().getFd();
pfd.events = POLLIN;
pfd.revents = 0;
_pollfds.push_back(pfd);
}
catch (const std::runtime_error& e)
{
std::cerr << "Error: " << e.what() << std::endl;
continue;
}
}
}

if (_serverSockets.empty())
{
std::cerr << "Error: No valid server sockets created" << std::endl;
throw std::runtime_error("No valid server sockets created");
}
}

void webServer::setNonBlocking(int socket)
{
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags == -1)
    {
        std::cerr << "Error: Could not get socket flags" << std::endl;
        return;
    }
    if (fcntl(socket, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        std::cerr << "Error: Could not set socket to non-blocking" << std::endl;
    }
}

void webServer::start() {
    std::cout << "[SUCCESS] Server started on configured ports!" << std::endl;
    while (true) {
        int pollCount = poll(_pollfds.data(), _pollfds.size(), 500);  // 500ms timeout
        if (pollCount < 0) {
            std::cerr << "Error: Polling failed" << std::endl;
            continue;
        }

        for (int i = _pollfds.size() - 1; i >= 0; --i) {
            if (_pollfds[i].revents & POLLIN) {
                auto it = std::find_if(_serverSockets.begin(), _serverSockets.end(),
                    [fd = _pollfds[i].fd](const Socket& socket) {
                        return socket.getFd() == fd;
                    });
                if (it != _serverSockets.end()) {
                    int clientSocket = accept(_pollfds[i].fd, nullptr, nullptr);
                    if (clientSocket < 0) {
                        std::cerr << "Error: Could not accept connection" << std::endl;
                        continue;
                    }
                    setNonBlocking(clientSocket);
                    addConnection(clientSocket);
                } else {
                    processRead(_pollfds[i].fd);
                }
            }

            if (_pollfds[i].revents & POLLOUT) {
                processWrite(_pollfds[i].fd);
            }

            if (_pollfds[i].revents & (POLLERR | POLLHUP)) {
                closeConnection(_pollfds[i].fd);
            }
        }
    }
}

std::string webServer::executeCGI(const std::string& scriptPath, const std::string& method, const std::string& queryString, const std::string& requestBody)
{
    // Remove query string from the script path
    std::string cleanScriptPath = scriptPath;
    size_t queryPos = cleanScriptPath.find('?');
    if (queryPos != std::string::npos) {
        cleanScriptPath = cleanScriptPath.substr(0, queryPos);
    }

    int pipeToChild[2];  // Parent writes to child (POST data)
    int pipeFromChild[2]; // Parent reads from child (CGI output)

    if (pipe(pipeToChild) == -1 || pipe(pipeFromChild) == -1) {
        return "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\nFailed to create pipes";
    }

    pid_t pid = fork();
    if (pid == -1) {
        return "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\nFailed to fork";
    }

    if (pid == 0) { // Child process (CGI script)
        close(pipeToChild[1]); // Close write end of pipeToChild
        close(pipeFromChild[0]); // Close read end of pipeFromChild

        // Redirect stdin and stdout
        dup2(pipeToChild[0], STDIN_FILENO);
        dup2(pipeFromChild[1], STDOUT_FILENO);

        // Set environment variables
        setenv("REQUEST_METHOD", method.c_str(), 1);
        setenv("QUERY_STRING", queryString.c_str(), 1);
        setenv("CONTENT_LENGTH", std::to_string(requestBody.size()).c_str(), 1);
        setenv("CONTENT_TYPE", "application/x-www-form-urlencoded", 1); // Adjust as needed
        setenv("SCRIPT_NAME", cleanScriptPath.c_str(), 1);
        setenv("PATH_INFO", "", 1); // Adjust if additional path info is present

        // Execute the CGI script
        execl(cleanScriptPath.c_str(), cleanScriptPath.c_str(), nullptr);

        // If execl fails
        std::cerr << "Failed to execute CGI script: " << cleanScriptPath << " (Error: " << strerror(errno) << ")" << std::endl;
        exit(1);
    } else { // Parent process (server)
        close(pipeToChild[0]); // Close read end of pipeToChild
        close(pipeFromChild[1]); // Close write end of pipeFromChild

        // Write POST data to the CGI script (if any)
        if (method == "POST" && !requestBody.empty()) {
            write(pipeToChild[1], requestBody.c_str(), requestBody.size());
        }
        close(pipeToChild[1]); // Close write end after writing

        // Read the CGI output
        std::string cgiOutput;
        char buffer[4096];
        fd_set readSet;
        int maxFd = pipeFromChild[0];

        while (true) {
            FD_ZERO(&readSet);
            FD_SET(pipeFromChild[0], &readSet);

            int activity = select(maxFd + 1, &readSet, nullptr, nullptr, nullptr);
            if (activity < 0) {
                std::cerr << "select() error" << std::endl;
                break;
            }

            if (FD_ISSET(pipeFromChild[0], &readSet)) {
                ssize_t bytesRead = read(pipeFromChild[0], buffer, sizeof(buffer));
                if (bytesRead > 0) {
                    cgiOutput.append(buffer, bytesRead);
                } else if (bytesRead == 0) {
                    break; // EOF
                } else {
                    std::cerr << "read() error" << std::endl;
                    break;
                }
            }
        }

        close(pipeFromChild[0]); // Close read end after reading

        // Wait for the child process to finish
        int status;
        waitpid(pid, &status, 0);

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        {
            return cgiOutput; // Return the CGI output as the HTTP response
        } else {
            return "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\nCGI script failed";
        }
    }
}
void webServer::addConnection(int clientFd)
{
    Connection conn;
    conn.socket = Socket(clientFd);
    conn.requestComplete = false;
    _connections[clientFd] = std::move(conn);

    struct pollfd pfd;
    pfd.fd = clientFd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    _pollfds.push_back(pfd);
}

void webServer::processRead(int clientSocket)
{
    // Only do one read per poll event
    char buffer[1024];
    ssize_t bytesRead = read(clientSocket, buffer, sizeof(buffer));

    if (bytesRead > 0)
    {
        _connections[clientSocket].inputBuffer.append(buffer, bytesRead);
        if (_connections[clientSocket].inputBuffer.find("\r\n\r\n") != std::string::npos)
        {
            _connections[clientSocket].requestComplete = true;
            _connections[clientSocket].outputBuffer = handleRequest(_connections[clientSocket].inputBuffer);
            updatePollEvents(clientSocket, POLLOUT);
        }
    }
    else if (bytesRead == 0)
    {
        closeConnection(clientSocket);
    }
    else // bytesRead < 0
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            std::cerr << "Error during read: " << strerror(errno) << std::endl;
            closeConnection(clientSocket);
        }
    }
}

void webServer::processWrite(int clientSocket)
{
    Connection &conn = _connections[clientSocket];
    if (!conn.outputBuffer.empty())
    {
        ssize_t bytesWritten = write(conn.socket.getFd(), conn.outputBuffer.c_str(), conn.outputBuffer.size());

        if (bytesWritten > 0)
        {
            conn.outputBuffer.erase(0, bytesWritten);
            if (conn.outputBuffer.empty())
            {
                closeConnection(clientSocket);
            }
        }
        else if (bytesWritten == 0)
        {
            closeConnection(clientSocket);
        }
        else
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
                std::cerr << "Error during write: " << strerror(errno) << std::endl;
                closeConnection(clientSocket);
            }
        }
    }
}

void webServer::updatePollEvents(int fd, short newEvent)
{
    auto it = std::find_if(_pollfds.begin(), _pollfds.end(), 
        [fd](const struct pollfd& pfd) { return pfd.fd == fd; });
    
    if (it != _pollfds.end()) {
        it->events = newEvent;
    }
}

void webServer::closeConnection(int clientSocket)
{
    _connections.erase(clientSocket);
    _pollfds.erase(std::remove_if(_pollfds.begin(), _pollfds.end(), [clientSocket](const pollfd &p)
    {
        return p.fd == clientSocket;
    }), _pollfds.end());
}

std::string webServer::handleRequest(const std::string& fullRequest)
{
    if (fullRequest.empty())
    {
        return "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nEmpty request";
    }

    std::istringstream requestStream(fullRequest);
    std::string method, path, version;
    requestStream >> method >> path >> version;

    if (method.empty() || path.empty() || version.empty())
    {
        return "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nMalformed Request";
    }

    std::unordered_map<std::string, std::string> headerMap;
    std::string line;
    std::getline(requestStream, line);

    while (std::getline(requestStream, line) && line != "\r")
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

    std::string requestBody;
    if (headerMap.find("Content-Length") != headerMap.end())
    {
        size_t headerEnd = fullRequest.find("\r\n\r\n");
        if (headerEnd != std::string::npos)
        {
            requestBody = fullRequest.substr(headerEnd + 4);
        }
    }

    path = sanitizePath(path);
    if (path.empty() || path[0] != '/')
    {
        return "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nInvalid path";
    }

    std::string rootDir = "./www";
    auto it = _serverConfig.find("root");
    if (it != _serverConfig.end())
    {
        rootDir = it->second;
    }

    if (path.find("/cgi-bin/") == 0)
    {
        std::string scriptPath = rootDir + path;
        std::string queryString;

        size_t queryPos = path.find('?');
        if (queryPos != std::string::npos)
        {
            queryString = path.substr(queryPos + 1);
            path = path.substr(0, queryPos);
        }

        return executeCGI(scriptPath, method, queryString, requestBody);
    }

    if (path.back() == '/')
    {
        path += "index.html";
    }

    std::string filePath = rootDir + path;
    std::cout << method << " " << path << " " << version << std::endl;

    std::string contentType = "application/x-www-form-urlencoded";
    auto ctIt = headerMap.find("Content-Type");
    if (ctIt != headerMap.end())
    {
        contentType = ctIt->second;
    }

    if (method == "GET")
    {
        return generateGetResponse(filePath);
    }
    else if (method == "POST")
    {
        return generatePostResponse(requestBody, contentType);
    }
    else if (method == "DELETE")
    {
        return generateDeleteResponse(filePath);
    }
    else
    {
        return generateMethodNotAllowedResponse();
    }
}

std::string webServer::generateGetResponse(const std::string& filePath)
{
    struct stat fileStat;
    if (stat(filePath.c_str(), &fileStat) == -1)
    {
        std::string contentType;
        std::string errorPage = getDefaultErrorPage(404, contentType);
        
        std::stringstream response;
        response << "HTTP/1.1 404 Not Found\r\n"
                 << "Content-Type: " << contentType << "\r\n"
                 << "Content-Length: " << errorPage.size() << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n"
                 << errorPage;
        return response.str();
    }
    
    if (access(filePath.c_str(), R_OK) == -1)
    {
        std::string contentType;
        std::string errorPage = getDefaultErrorPage(403, contentType);
        
        std::stringstream response;
        response << "HTTP/1.1 403 Forbidden\r\n"
                 << "Content-Type: " << contentType << "\r\n"
                 << "Content-Length: " << errorPage.size() << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n"
                 << errorPage;
        return response.str();
    }
    
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
    {
        std::string contentType;
        std::string errorPage = getDefaultErrorPage(500, contentType);
        
        std::stringstream response;
        response << "HTTP/1.1 500 Internal Server Error\r\n"
                 << "Content-Type: " << contentType << "\r\n"
                 << "Content-Length: " << errorPage.size() << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n"
                 << errorPage;
        return response.str();
    }

    std::string contentType = getContentType(filePath);
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<char> fileContent(fileSize);
    file.read(fileContent.data(), fileSize);
    file.close();
    
    std::stringstream responseHeader;
    responseHeader << "HTTP/1.1 200 OK\r\n"
                   << "Content-Type: " << contentType << "\r\n"
                   << "Content-Length: " << fileSize << "\r\n"
                   << "Connection: close\r\n"
                   << "\r\n";
    
    std::string header = responseHeader.str();
    return header + std::string(fileContent.data(), fileSize);
}


std::string webServer::getContentType(const std::string& filePath)
{
    size_t dotPos = filePath.find_last_of(".");
    if (dotPos == std::string::npos)
	{
        return "application/octet-stream";
    }
    
    std::string ext = filePath.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    if (ext == "html" || ext == "htm") return "text/html";
    if (ext == "css") return "text/css";
    if (ext == "js") return "application/javascript";
    if (ext == "json") return "application/json";
    if (ext == "txt") return "text/plain";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "png") return "image/png";
    if (ext == "gif") return "image/gif";
    if (ext == "ico") return "image/x-icon";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "pdf") return "application/pdf";
    
    return "application/octet-stream";
}

std::string webServer::processRequest(const std::string& request)
{
    std::string method, path, version;
    std::istringstream requestStream(request);
    requestStream >> method >> path >> version;
    
    std::map<std::string, std::string> headers;
    std::string line;
    while (std::getline(requestStream, line) && line != "\r")
    {
        size_t colonPos = line.find(":");
        if (colonPos != std::string::npos)
        {
            std::string headerName = line.substr(0, colonPos);
            std::string headerValue = line.substr(colonPos + 1);
            headerValue.erase(0, headerValue.find_first_not_of(" "));
            headerValue.erase(headerValue.find_last_not_of("\r") + 1);
            headers[headerName] = headerValue;
        }
    }
    std::string requestBody;
    if (method == "POST")
    {
        std::stringstream bodyStream;
        bodyStream << requestStream.rdbuf();
        requestBody = bodyStream.str();
    }
    
    if (method == "GET")
    {
        return generateGetResponse(getFilePath(path));
    }
    else if (method == "POST")
    {
        std::string contentType = "text/plain";
        auto it = headers.find("Content-Type");
        if (it != headers.end())
        {
            contentType = it->second;
        }
        return generatePostResponse(requestBody, contentType);
    }
    else if (method == "DELETE")
    {
        return generateDeleteResponse(getFilePath(path));
    }
    else
    {
        return generateMethodNotAllowedResponse();
    }
}

std::string webServer::generatePostResponse(const std::string& requestBody, const std::string& contentType)
{
    std::string uploadDir = "./www/html/uploads";
    auto it = _serverConfig.find("upload_dir");
    if (it != _serverConfig.end())
    {
        uploadDir = it->second;
    }

    std::filesystem::create_directories(uploadDir);
    
    if (contentType.find("multipart/form-data") != std::string::npos)
    {
        std::string filename = "default_upload.txt";
        size_t filenamePos = requestBody.find("filename=\"");
        if (filenamePos != std::string::npos)
        {
            size_t start = filenamePos + 10;
            size_t end = requestBody.find("\"", start);
            if (end != std::string::npos)
            {
                filename = requestBody.substr(start, end - start);
                filename = sanitizeFilename(filename);
            }
        }

        std::string filePath = uploadDir + "/" + filename;
        size_t contentStart = requestBody.find("\r\n\r\n");
        if (contentStart != std::string::npos)
        {
            contentStart += 4;
        }
        else
        {
            return "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nMalformed request";
        }

        std::ofstream file(filePath, std::ios::out | std::ios::binary);
        if (!file)
        {
            return "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\nFailed to save file";
        }
        
        file.write(requestBody.data() + contentStart, requestBody.size() - contentStart);
        file.close();
        std::cout << "[POST] File saved: " << filename << std::endl;
    }
    else if (contentType.find("application/x-www-form-urlencoded") != std::string::npos)
    {
        std::string filename = "form_data_" + getCurrentTimeString() + ".txt";
        std::string filePath = uploadDir + "/" + filename;
        
        std::ofstream file(filePath, std::ios::out);
        if (!file)
        {
            return "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\nFailed to save file";
        }
        file << requestBody;
        file.close();
        std::cout << "[POST] Form data saved: " << filename << std::endl;
    }
    else
    {
        return "HTTP/1.1 415 Unsupported Media Type\r\nContent-Type: text/plain\r\n\r\nUnsupported content type";
    }
    
    std::stringstream response;
    response << "HTTP/1.1 201 Created\r\n"
             << "Content-Type: text/plain\r\n\r\n"
             << "File uploaded successfully!";
    
    return response.str();
}

std::string webServer::getCurrentTimeString()
{
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_time_t), "%Y%m%d_%H%M%S");
    return ss.str();
}

std::string webServer::generateDeleteResponse(const std::string& filePath)
{
    std::string adjustedFilePath = filePath;
    size_t pos = filePath.find("/uploads/");
    if (pos != std::string::npos)
    {
         std::string filename = filePath.substr(pos + std::string("/uploads/").length());
         adjustedFilePath = "./www/uploads/" + filename;
    }
    
    if (!std::filesystem::exists(adjustedFilePath))
    {
        std::cerr << "[DELETE] File not found: " << adjustedFilePath << std::endl;
        return "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nFile not found";
    }

    if (!std::filesystem::is_regular_file(adjustedFilePath))
    {
        return "HTTP/1.1 403 Forbidden\r\nContent-Type: text/plain\r\n\r\nCannot delete directories";
    }
    
    try
    {
        if (std::filesystem::remove(adjustedFilePath))
        {
            std::cout << "[DELETE] Successfully deleted file: " << adjustedFilePath << std::endl;
            return "HTTP/1.1 204 No Content\r\n\r\n";
        }
        else
        {
            std::cerr << "[DELETE] Failed to delete file: " << adjustedFilePath << std::endl;
            return "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\nFailed to delete file";
        }
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        std::cerr << "[DELETE] Filesystem error: " << e.what() << std::endl;
        return "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\nFailed to delete file";
    }
}

void webServer::sendResponse(Socket& clientSocket, const std::string& response) {
    ssize_t bytesWritten = write(clientSocket.getFd(), response.c_str(), response.size());
    if (bytesWritten < 0) {
        std::cerr << "Error sending response: " << strerror(errno) << std::endl;
    }
}

std::string webServer::getDefaultErrorPage(int errorCode, std::string& contentType)
{
    const std::string basePath = "./www/html/error_pages/";
    const std::string imagePath = basePath + std::to_string(errorCode) + ".jpg";
    const std::string defaultImagePath = basePath + "default.jpg";
    
    std::cout << "[INFO] Serving error page for code: " << errorCode << std::endl;
    
    std::ifstream imageFile(imagePath, std::ios::binary);
    if (imageFile.good())
	{
        try {
            std::stringstream imageStream;
            imageStream << imageFile.rdbuf();
            
            if (imageStream.fail())
			{
                std::cerr << "[WARN] Failed to read error image: " << imagePath << std::endl;
            }
			else
			{
                contentType = "image/jpeg";
                return imageStream.str();
            }
        }
		catch (const std::exception& e) {
            std::cerr << "[ERROR] Exception reading error image: " << e.what() << std::endl;
        }
        imageFile.close();
    }

    std::ifstream defaultImageFile(defaultImagePath, std::ios::binary);
    if (defaultImageFile.good())
	{
        try {
            std::stringstream imageStream;
            imageStream << defaultImageFile.rdbuf();
            
            if (imageStream.fail())
			{
                std::cerr << "[WARN] Failed to read default error image" << std::endl;
            }
			else
			{
                contentType = "image/jpeg";  
                return imageStream.str();
            }
        }
		catch (const std::exception& e)
		{
            std::cerr << "[ERROR] Exception reading default error image: " << e.what() << std::endl;
        }
        defaultImageFile.close();
    }
	else
	{
        std::cerr << "[ERROR] Missing default error image: " << defaultImagePath << std::endl;
    }

    contentType = "text/plain";
    return "Error " + std::to_string(errorCode) + ": Missing error page.";
}

std::string webServer::generateMethodNotAllowedResponse()
{
    std::string contentType;
    std::string errorPage = getDefaultErrorPage(405, contentType);
    
    std::stringstream response;
    response << "HTTP/1.1 405 Method Not Allowed\r\n"
             << "Content-Type: " << contentType << "\r\n"
             << "Content-Length: " << errorPage.size() << "\r\n"
             << "Allow: GET, POST, DELETE\r\n"
             << "Connection: close\r\n"
             << "\r\n"
             << errorPage;
             
    return response.str();
}

std::string webServer::getFilePath(const std::string& path)
{
    std::string basePath = "./public";
    if (path == "/" || path.empty())
	{
        return basePath + "/index.html";
    }
    return basePath + path;
}
