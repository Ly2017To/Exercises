#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "gpio.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <poll.h>
#include <unistd.h>

int gpio_export(int gpio_num) 
{
  int gpioFd;
  char buf[50];
  int ret;

  gpioFd = open("/sys/class/gpio/export", O_WRONLY | O_SYNC);

  if(gpioFd != -1) {
    sprintf(buf, "%d", gpio_num);
    ret = write(gpioFd, buf, strlen(buf));
    if(ret < 0){
      return -2;
    }
    close(gpioFd);
  }else {
      return -1;
  }

  return 0;
}

int gpio_direction(int gpio_num, char direction[])
{
  char buf[50];
  int ret, gpioFd;
  sprintf(buf, "/sys/class/gpio/gpio%d/direction", gpio_num);
  gpioFd = open(buf, O_WRONLY);
  if(gpioFd != -1) {
    //sprintf(buf, "%s", direction);
    ret = write(gpioFd, direction, strlen(direction));
    if(ret < 0){
      return -2;
    }
    close(gpioFd);
  }else {
      return -1;
  }

  return 0;
}

int gpio_edge(int gpio_num, char edge[])
{
  char buf[50];
  int ret, gpioFd;
  sprintf(buf, "/sys/class/gpio/gpio%d/edge", gpio_num);
  gpioFd = open(buf, O_WRONLY);

  if(gpioFd != -1) {
    //sprintf(buf, "%s", edge);
    ret = write(gpioFd, edge, strlen(edge));
    if(ret < 0){
      return -2;
    }
    close(gpioFd);
  }else {
      return -1;
  }

  return 0;
}

void gpio_interrupt(int gpio_num)
{
  char buff[50];
  int ret, gpio_fd;
  struct pollfd pfd[1];

  sprintf(buff, "/sys/class/gpio/gpio%d/value", gpio_num);
  gpio_fd = open(buff, O_RDONLY);
  if( gpio_fd == -1 ){
    perror("error:gpio read\n");
  }

  //listen to event
  pfd[0].fd = gpio_fd;
  pfd[0].events  = POLLPRI;
  
  //read to clear the flag
  ret = read(gpio_fd,buff,10);
  if( ret == -1 ){
    perror("read");
  }

  while(1){
    ret = poll(pfd,1,-1);
    if( ret == -1 ){
      perror("poll");
    }
    if(pfd[0].revents & POLLPRI){
      //reposition offset
      ret = lseek(gpio_fd,0,SEEK_SET);
      if( ret == -1 ){
        perror("lseek");
      }
      ret = read(gpio_fd,buff,10);
      if( ret == -1 ){
        perror("read");
      }
      //got trigger signal    
      printf("get interrupt\n");
    }
  }

  return;
}

int gpio_unexport(int gpio_num) 
{
  int gpioFd, ret;
  char buf[50];

  gpioFd = open("/sys/class/gpio/unexport", O_WRONLY | O_SYNC);
  if(gpioFd != -1) {
      sprintf(buf, "%d", gpio_num);
      ret = write(gpioFd, buf, strlen(buf));
      if(ret < 0){
        return -2;
      } 
      close(gpioFd);
  }else {
      return -1;
  }  

  return 0;
}
