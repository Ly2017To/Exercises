#include "Configurations.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <sys/stat.h>  // For chmod
#include "json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;  // Alias for filesystem

Configurations::Configurations(const std::string& configFile) : configFile(configFile) {
    loadConfig();
}

void Configurations::loadConfig() {
	std::ifstream file(configFile);
    if (!file.is_open()) {
       //std::cerr << "Failed to open configuration file: " << configFile << std::endl;
		Logger::getInstance().log("Failed to open configuration file: " + configFile,Logger::PROD);
    	return;
   }else{
		Logger::getInstance().log("open configuration file successfully: " + configFile,Logger::PROD);
	}

  	json config;
   	file >> config;
	
	file.close();

   	testLog = config["testLog"].get<bool>();
	sysPort = config["port"];
	folderCommon = config["folderCommon"];

	// Iterating through the cameras array
    for (const auto& camera : config["cameras"]) {
		CameraConfig cfg;
		cfg.name =camera["name"];
		cfg.enable =  camera["enable"].get<bool>();
		cfg.type = camera["type"];
		cfg.ip = camera["ip"];
		cfg.port = camera["port"] ;
		cfg.username = camera["username"];
		cfg.password = camera["password"];
	   // Check if folderCommon is a valid string (non-empty)
		if (!folderCommon.empty()) {
            // If valid, use it
			cfg.folder = folderCommon;
		} else {
            // Otherwise, check if camera["folder"] is a valid string
			if (camera["folder"].is_string()) {
                cfg.folder = camera["folder"].get<std::string>();
					if(cfg.folder.empty()){
						cfg.folder = "../pictures/"; // Or some default folder path
					}
			} else {
                // If neither are valid, assign a default value (empty string or any default path)
				cfg.folder = "../pictures/"; // Or some default folder path
			}
		}
		cameraConfigs.push_back(cfg);
    }
}

std::vector<std::shared_ptr<Camera>> Configurations::createCameras() {
	std::vector<std::shared_ptr<Camera>> cameras;
    for (const auto& cfg : cameraConfigs) {
		std::shared_ptr<Camera> camera;

        if (cfg.type == "HikCamera") {
            camera = std::make_shared<HikCamera>();
      } 
        // Extend here for other types of cameras.

        if (camera) {
			camera->setCameraParams(cfg.name, cfg.enable,,cfg.type,cfg.ip, cfg.port,cfg.username, cfg.password, cfg.folder);
      		cameras.push_back(camera);
      }
	}
    return cameras;
}

void Configurations::createCameraFolders() {
    for (const auto& cfg : cameraConfigs) {
        fs::path folderPath = cfg.folder;  // e.g., base folder + camera name
        try {
            if (!fs::exists(folderPath)) {
                // Create the directory
                fs::create_directories(folderPath);
                Logger::getInstance().log("Created folder for camera: " + folderPath.string(), Logger::PROD);

                // Set permissions to 777 (read, write, and execute for owner, group, and others)
                if (chmod(folderPath.c_str(), S_IRWXU | S_IRWXG | S_IRWXO) != 0) {
                    Logger::getInstance().log("Error setting permissions for folder: " + folderPath.string(), Logger::PROD);
                } else {
                    Logger::getInstance().log("Set 777 permissions for folder: " + folderPath.string(), Logger::PROD);
                }
            } else {
                Logger::getInstance().log("Folder already exists for camera: " + folderPath.string(), Logger::PROD);
            }
        } catch (const fs::filesystem_error& e) {
            Logger::getInstance().log("Error creating folder for camera " + cfg.name + ": " + e.what(), Logger::PROD);
        }
    }
}
