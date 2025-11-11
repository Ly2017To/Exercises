#ifndef CONFIGURATIONS_H
#define CONFIGURATIONS_H

#include <string>
#include <vector>
#include <memory>
#include "Camera.h"
#include "HikCamera.h"
#include "logger.h"

class Configurations {
public:
    explicit Configurations(const std::string& configFile);
	std::vector<std::shared_ptr<Camera>> createCameras();
	int getSysPort() const {return sysPort;}
	void createCameraFolders();
	bool getTestLog() const {return testLog;}

private:
	std::string configFile;

    bool testLog;
	int sysPort;
	std::string folderCommon; 

    struct CameraConfig {
		std::string name;
        bool enable;
		std::string type;
		std::string ip;
        int port;
		std::string username;
		std::string password;
		std::string folder;
	};

	std::vector<CameraConfig> cameraConfigs;
    void loadConfig();
};

#endif
