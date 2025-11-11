This is an application to control camera taking pictures based on commands received.

If a camera receives TRIGGER ON command, then start a thread to take pictures.

If a camera receives TRIGGER ON command, then close the thread.

Here is the project structure:

camera_control/
├── CMakeLists.txt
├── src/ 
│   ├── main.cpp
│   ├── MessageListener.cpp
│   ├── MainController.cpp
│   ├── Camera.cpp
│   ├── HikCamera.cpp
│   ├── Logger.cpp
├── include/ 
│   ├── json.hpp
│   ├── Camera.h
│   ├── HikCamera.h
│   ├── MessageListener.h
│   ├── MainController.h
│   ├── Utility.h
│   ├── Logger.h
├── lib/ 
└── build/ 

To build the project:
mkdir build
cd build
cmake ..
make
make install

The folder install is used as production folder. The execution is located at bin folder. 


