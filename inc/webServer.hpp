/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   webServer.hpp                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: piotr <piotr@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/02/16 21:11:58 by anamieta          #+#    #+#             */
/*   Updated: 2025/03/18 10:35:58 by piotr            ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#pragma once

#include <unordered_map>
#include <vector>
#include <netinet/in.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <array>
#include <filesystem>
#include <csignal>
#include <sys/stat.h>
#include "socket.hpp"
#include "SocketManager.hpp"
#include "HTTPRequest.hpp"
#include "parseConfig.hpp"
#include <map>

class webServer {
    public:
        // Constructor
        webServer(const std::unordered_multimap<std::string, std::string>& serverConfig,
                  const std::unordered_multimap<std::string, std::vector<std::string>>& locationConfig);
    
        // Struct for CGI configuration
        struct CGIConfig 
        {
            std::string cgiPass;
            std::string scriptFilename;
            std::string pathInfo;
            std::string queryString;
            std::string requestMethod;
        };
        
        // Server control functions
        void start();
        void addConnection(int clientFd, const std::string& serverName);
        void closeConnection(int fd);
    
        // Request handling
        std::string readFullRequest(int clientSocket);
        std::string processRequest(const std::string& request);
        std::string handleRequest(const std::string& fullRequest);
        
        // Response handling
        void sendResponse(Socket& clientSocket, const std::string& response);
        std::string generateResponse(const HTTPRequest& request);
        std::string generateDeleteResponse(const std::string& filePath);
        std::string generateMethodNotAllowedResponse();
        std::string generatePostResponse(const std::string& requestBody, const std::string& contentType, const std::string& serverName);
        std::string generateGetResponse(const std::string& filePath);
        std::string generateErrorResponse(int statusCode, const std::string& message);
        std::string generateSuccessResponse(const std::string& message);
        std::string generateDirectoryListing(const std::string& directoryPath, const std::string& requestPath);
    
        // Configuration setters
        void setAutoindexConfig(const std::map<std::string, bool>& autoindexConfig);
        void setRedirections(const std::map<std::string, std::string>& redirections);
        void setServerNames(const std::map<std::string, std::string>& serverNames);
        void setClientMaxBodySize(const std::string& serverName, size_t size);
        void setRootDirectories(const std::map<std::string, std::string>& rootDirectories);
        void setAllowedMethods(const std::map<std::string, std::vector<std::string>>& allowedMethods);
        void setCGIConfig(const std::map<std::string, CGIConfig>& cgiConfig);
    
        // Configuration getters
        size_t getClientMaxBodySize(const std::string& serverName) const;
        size_t getContentLength(const std::unordered_map<std::string, std::string>& headers);
        std::vector<std::string> getAllowedMethods(const std::string& location) const;
        const std::map<std::string, std::vector<std::string>>& getAllowedMethods() const;
        const std::map<std::string, CGIConfig>& getCGIConfigs() const;
        const CGIConfig& getCGIConfig(const std::string& location) const;
    
        // Utility functions
        std::string sanitizePath(const std::string& path);
        std::string sanitizeFilename(const std::string& filename);
        std::string getCurrentTimeString();
        std::string getStatusMessage(int statusCode);
        std::string getFilePath(const std::string& path);
        std::string resolveFilePath(const std::string& path, const std::string& rootDir);
        
        // Upload handling
        std::string handleMultipartUpload(const std::string& requestBody, const std::string& contentType, const std::string& uploadDir);
        std::string handleFormUrlEncodedUpload(const std::string& requestBody, const std::string& uploadDir);
        std::string handleTextUpload(const std::string& requestBody, const std::string& uploadDir);
    
        // CGI execution
        std::string executeCGI(const std::string& scriptPath, const std::string& method, const std::string& queryString, const std::string& requestBody);
    
        // Public member variables
        int _formNumber = 0;
    
    private:
        // Struct for active connection management
        struct Connection 
        {
            Socket socket;
            std::string inputBuffer;
            std::string outputBuffer;
            bool requestComplete;
            int cgiPipeIn[2];
            int cgiPipeOut[2];
            pid_t cgiPid;
            std::string serverName;
        };
    
        // Internal request processing
        void processRead(int clientSocket);
        void processWrite(int clientSocket);
        void updatePollEvents(int fd, short newEvent);
    
        // Member variables
        std::map<std::string, CGIConfig> _cgiConfig;
        std::map<std::string, std::vector<std::string>> _allowedMethods;
        std::map<std::string, std::string> _aliasDirectories;
        std::map<std::string, size_t> _clientMaxBodySizes;
        std::map<std::string, bool> _autoindexConfig;
        std::map<std::string, std::string> _serverNames;
        std::map<std::string, std::string> _redirections;
        std::map<std::string, std::string> _rootDirectories;
        std::unordered_multimap<std::string, std::string> _serverConfig;
        std::unordered_multimap<std::string, std::vector<std::string>> _locationConfig;
        std::unordered_map<int, Connection> _connections;
        std::vector<struct pollfd> _pollfds;
        
        // Parsing functions
        std::unordered_map<std::string, std::string> parseHeaders(const std::string& headerSection);
    
        // Socket manager instance
        SocketManager _socketManager;
};
