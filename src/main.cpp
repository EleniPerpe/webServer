/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: piotr <piotr@student.42.fr>                +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/02/12 14:42:10 by eleni             #+#    #+#             */
/*   Updated: 2025/03/04 20:03:02 by piotr            ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "parseConfig.hpp"
#include "webServer.hpp"
#include "utils.hpp"
#include <thread>
#include <vector>

int main(int argc, char** argv)
{
    std::string filename;
    if (argc > 2)
    {
        std::cout << "Usage: ./webserv <file.config>" << std::endl;
        return 1;
    }
    else if (argc == 1)
        filename = "config/config1.config";
    else if (argc == 2)
        filename = argv[1];

    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error: Could not open configuration file: " << filename << std::endl;
        return 1;
    }

    std::vector<parseConfig> parser = splitServers(file);
    file.close();

    std::vector<std::thread> threads;
    for (size_t j = 0; j < parser.size(); j++)
    {
        try
        {
            parser[j].parse(parser[j]._mainString);
            webServer server(parser[j]._parsingServer, parser[j]._parsingLocation);

            threads.push_back(std::thread([server]() mutable
			{
                server.start();
            }));
        }
        catch(const std::exception& e)
        {
            std::cerr << e.what() << '\n';
        }
    }
    for(auto &t : threads)
    {
        if (t.joinable())
            t.join();
    }

    return 0;
}