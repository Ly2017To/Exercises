/**
 * @file fpga_gpio.c
 *
 * @brief a program implements the following scheme on TLT3F-EVM: arm receives gpio signal from fpga.
 *
 * @author luca
 *
 * @version V1.0
 *
 * @date 2023-12-28
 **/

#include <stdio.h>
#include <stdbool.h>
#include <getopt.h>
#include <string.h>
#include <libgen.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include "gpio.h"


int main(int argc, char **argv)
{
   printf("test data interaction between fpga and arm\n");

   //export gpio 
   if(gpio_export(PH2_GPIO_NUM)!=0){
     perror("error:export gpio\n");
   }else{
     printf("export gpio successfully\n");
   }
	   
   //set gpio direction
   if(gpio_direction(PH2_GPIO_NUM,DIRECTION_IN)!=0){
     perror("error:set gpio direction\n");
   }else{
     printf("set gpio direction successfully\n");
   }

   //set gpio trigger edge
   if(gpio_edge(PH2_GPIO_NUM,EDGE_RISING)!=0){
     perror("error:set gpio trigger edge\n");
   }else{
     printf("set gpio trigger edge successfully\n");
   }
   
   //gpio interrupt
   gpio_interrupt(PH2_GPIO_NUM);

   //unexport gpio
   if(gpio_unexport(PH2_GPIO_NUM)!=0){
     perror("error:unexport gpio\n");
   }else{
     printf("unexport gpio successfully\n");
   }

   return EXIT_SUCCESS;
}
