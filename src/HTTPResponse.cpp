/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HTTPResponse.cpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: anamieta <anamieta@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/03/07 18:50:13 by pwojnaro          #+#    #+#             */
/*   Updated: 2025/03/16 21:17:18 by anamieta         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "HTTPResponse.hpp"
#include <algorithm>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <filesystem>
#include <iostream>
#include <vector>
#include <optional>


//This initializes an HTTPResponse object with a given status code, content type, and body.
HTTPResponse::HTTPResponse(int statusCode, const std::string& contentType, const std::string& body)
    : statusCode(statusCode), contentType(contentType), body(body) {}

//Generates raw HTTP response
std::string HTTPResponse::generateResponse() const
{
    std::ostringstream response;
    response << "HTTP/1.1 " << statusCode << "\r\n"
             << "Content-Type: " << contentType << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n"
             << "\r\n"
             << body;
    return response.str();
}


/** This function attempts to read an image file in binary mode and return its contents as a std::string.
* By default, files in C++ are opened in text mode, jpg has to be open in binary mode. We have to to know the size
* to set Content-Length correctly in the HTTP response
*/
std::optional<std::string> readImageFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
	{
        std::cerr << "[WARN] Could not open image: " << path << std::endl;
        return std::nullopt;
    }
	// Move cursor to end to determine size
    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
	// Move cursor back to the beginning
    file.seekg(0, std::ios::beg);

	// Allocate buffer and read file data
    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size))
	{
        std::cerr << "[WARN] Failed to read image: " << path << std::endl;
        return std::nullopt;
    }

    return std::string(buffer.data(), size);
}

//Attempts to serve an error page based on errorCode. Searches for an error page image in ./www/html/error_pages/
//If not found, falls back to default.jpg. If no images exist, returns a plain text fallback

std::pair<std::string, std::string> HTTPResponse::getDefaultErrorPage(int errorCode)
{
    const std::string basePath = "./www/html/error_pages/";
    const std::string imagePath = basePath + std::to_string(errorCode) + ".jpg";
    const std::string defaultImagePath = basePath + "default.jpg";

    std::cout << "[INFO] Serving error page for code: " << errorCode << std::endl;

    auto content = readImageFile(imagePath);
    if (content)
    {
        return {*content, "image/jpeg"};
    }

    auto defaultContent = readImageFile(defaultImagePath);
    if (defaultContent)
    {
        return {*defaultContent, "image/jpeg"};
    }

    std::cerr << "[ERROR] Missing default error image: " << defaultImagePath << std::endl;
    std::cout << "[INFO] Returning text fallback for error " << errorCode << std::endl;
    return {"Error " + std::to_string(errorCode) + ": Missing error page.", "text/plain"};
}


/**
 * When a web server sends a response, it must include the Content-Type header so
 * that browsers or other clients know how to handle the content.
 */
std::string HTTPResponse::getContentType(const std::string& filePath)
{
    size_t dotPos = filePath.find_last_of(".");
    if (dotPos == std::string::npos)
	{
        return "application/octet-stream"; //returns  generic binary data MIME type
    }

    std::string ext = filePath.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower); //changes .JPG to jpg

    if (ext == "html" || ext == "htm") return "text/html";
    if (ext == "css") return "text/css";
    if (ext == "js") return "application/javascript";
    if (ext == "txt") return "text/plain";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "png") return "image/png";
    if (ext == "ico") return "image/x-icon";

    return "application/octet-stream";
}
