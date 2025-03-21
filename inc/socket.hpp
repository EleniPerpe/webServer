/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   socket.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: pwojnaro <pwojnaro@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/03/05 20:32:32 by piotr             #+#    #+#             */
/*   Updated: 2025/03/21 21:02:07 by pwojnaro         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#pragma once

#include <stdexcept>
#include <unistd.h>
#include <sys/socket.h>
#include <string>

class Socket
{
public:
	Socket();
	Socket(int domain, int type, int protocol);
	Socket(int fd);
	Socket(int fd, const std::string& serverName);
	~Socket();
	Socket(const Socket&) = delete;
	Socket& operator=(const Socket&) = delete;
	
	int getFd() const;
	Socket(Socket&& other) noexcept;
	Socket& operator=(Socket&& other) noexcept;
	std::string getServerName() const;

private:
	int _fd;
	std::string _serverName;
};
