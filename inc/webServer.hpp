/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   webServer.hpp                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: piotr <piotr@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/02/16 21:11:58 by anamieta          #+#    #+#             */
/*   Updated: 2025/03/06 20:20:09 by piotr            ###   ########.fr       */
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

class webServer {
    public:
        webServer(const std::unordered_multimap<std::string, std::string>& serverConfig,
        const std::unordered_multimap<std::string, std::vector<std::string>>& locationConfig);
        void start();
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
    };
        std::vector<Socket> _serverSockets;
        std::unordered_map<int, Connection> _connections;
        std::string handleRequest(const std::string& fullRequest);
        std::string executeCGI(const std::string& scriptPath, const std::string& method, const std::string& queryString, const std::string& requestBody);
        void sendResponse(Socket& clientSocket, const std::string& response);
        void setNonBlocking(int socket);
		std::string generateDeleteResponse(const std::string& filePath);
		std::string generateMethodNotAllowedResponse();
        void addConnection(int clientFd);
        std::string processRequest(const std::string& request);
        void processRead(int clientSocket);
        void processWrite(int clientSocket);
        void updatePollEvents(int fd, short newEvent);
        void closeConnection(int fd);
		std::string generatePostResponse(const std::string& requestBody, const std::string& contentType);
		std::string sanitizePath(const std::string& path);
		std::string sanitizeFilename(const std::string& filename);
		std::string getContentType(const std::string& filePath);
		std::string generateGetResponse(const std::string& filePath);
		std::string getDefaultErrorPage(int errorCode, std::string& contentType);
        std::unordered_multimap<std::string, std::string> _serverConfig;
        std::unordered_multimap<std::string, std::vector<std::string>> _locationConfig;
        std::vector<struct pollfd> _pollfds;
		std::string getCurrentTimeString();
		std::string getFilePath(const std::string& path);
};