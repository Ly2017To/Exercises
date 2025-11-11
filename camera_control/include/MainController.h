#ifndef MAIN_CONTROLLER_H
#define MAIN_CONTROLLER_H

#include "Camera.h"
#include <memory>
#include <string>
#include <vector>
#include "Utility.h"
#include "logger.h"

class MainController {
public:
   MainController();  // Constructor
   void addCamera(const std::shared_ptr<Camera>& camera);
   void sendCommandToCamera(const std::string& cameraName, const std::string& command);
	void startAll();
	void stopAll(); 

private:
   std::vector<std::shared_ptr<Camera>> cameras;
	std::mutex mtx; 
};

#endif

