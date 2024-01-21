#ifndef GPIO_H_
#define GPIO_H_

//#define SYSFS_GPIO_EXPORT           "/sys/class/gpio/export"
//#define SYSFS_GPIO_UNEXPORT         "/sys/class/gpio/unexport"
//#define PH2_GPIO_NUM "226"

#define PH2_GPIO_NUM 226
#define DIRECTION_IN "in"
#define EDGE_RISING "rising"

int gpio_export(int);
int gpio_direction(int, char []);
int gpio_edge(int, char []);
void gpio_interrupt(int);
int gpio_unexport(int);


#endif /* GPIO_H_ */
