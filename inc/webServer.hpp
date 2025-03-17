/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   webServer.hpp                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: anamieta <anamieta@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/02/16 21:11:58 by anamieta          #+#    #+#             */
/*   Updated: 2025/03/17 15:55:11 by anamieta         ###   ########.fr       */
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
#include <map>

class webServer {
    public:
        webServer(const std::unordered_multimap<std::string, std::string>& serverConfig,
        const std::unordered_multimap<std::string, std::vector<std::string>>& locationConfig);
		void setAutoindexConfig(const std::map<std::string, bool>& autoindexConfig);
		void setRedirections(const std::map<std::string, std::string>& redirections);
		void setServerNames(const std::map<std::string, std::string>& serverNames);
		void setClientMaxBodySize(const std::string& serverName, size_t size);
		size_t getClientMaxBodySize(const std::string& serverName) const;
        size_t getContentLength(const std::unordered_map<std::string, std::string>& headers);
		void setRootDirectories(const std::map<std::string, std::string>& rootDirectories);
        void start();
		int _formNumber = 0;
		std::string readFullRequest(int clientSocket);
        std::vector<std::string> getAllowedMethods(const std::string& location) const {
            auto it = _allowedMethods.find(location);
            if (it != _allowedMethods.end()) {
                return it->second;
            }
            return {}; // Return an empty vector if no methods are specified
        }
        void setAllowedMethods(const std::map<std::string, std::vector<std::string>>& allowedMethods) {
            _allowedMethods = allowedMethods;
        }
		void setCgiInterpreter(const std::string& interpreter) {
			_cgiInterpreter = interpreter;
		}
		void setCgiRootDir(const std::string& rootDir) {
			_cgiRootDir = rootDir;
		}
        void setCgiPass(const std::string& cgiPass);
        std::string getCgiPass() const;
        void setScriptFilename(const std::string& scriptFilename);
        std::string getScriptFilename() const;

    private:
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
    	std::string _cgiInterpreter;
    	std::string _cgiRootDir;
        std::map<std::string, std::vector<std::string>> _allowedMethods;
		std::map<std::string, std::string> _aliasDirectories;
		std::map<std::string, size_t> _clientMaxBodySizes;
		std::map<std::string, bool> _autoindexConfig;
		std::map<std::string, std::string> _serverNames;
		std::map<std::string, std::string> _redirections;
		std::unordered_multimap<std::string, std::string> _serverConfig;
		std::unordered_multimap<std::string, std::vector<std::string>> _locationConfig;
		SocketManager _socketManager;
        std::unordered_map<int, Connection> _connections;
		std::map<std::string, std::string> _rootDirectories;
        std::string handleRequest(const std::string& fullRequest);
        void sendResponse(Socket& clientSocket, const std::string& response);
		std::string generateDeleteResponse(const std::string& filePath);
		std::string generateMethodNotAllowedResponse();
        void addConnection(int clientFd, const std::string& serverName);
        std::string processRequest(const std::string& request);
        void processRead(int clientSocket);
        void processWrite(int clientSocket);
        void updatePollEvents(int fd, short newEvent);
        void closeConnection(int fd);
		std::string generatePostResponse(const std::string& requestBody, const std::string& contentType, const std::string& serverName);
		std::string sanitizePath(const std::string& path);
		std::string sanitizeFilename(const std::string& filename);
		std::string generateGetResponse(const std::string& filePath);
        std::vector<struct pollfd> _pollfds;
		std::string getCurrentTimeString();
		std::string generateResponse(const HTTPRequest& request);
		std::string getStatusMessage(int statusCode);
		std::string getFilePath(const std::string& path);
		std::string generateErrorResponse(int statusCode, const std::string& message);
		std::unordered_map<std::string, std::string> parseHeaders(const std::string& headerSection);
		std::string resolveFilePath(const std::string& path, const std::string& rootDir);
		std::string generateDirectoryListing(const std::string& directoryPath, const std::string& requestPath);
		std::string executeCGI(const std::string& scriptPath, const std::string& method, const std::string& queryString, const std::string& requestBody);
        std::string generateSuccessResponse(const std::string& message);
		std::string handleMultipartUpload(const std::string& requestBody, const std::string& contentType, const std::string& uploadDir);
		std::string handleFormUrlEncodedUpload(const std::string& requestBody,const std::string& uploadDir);
		std::string handleTextUpload(const std::string& requestBody, const std::string& uploadDir);
        std::string cgiPass;
        std::string scriptFilename;
};
