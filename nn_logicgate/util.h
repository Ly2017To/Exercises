/*
 * util.h
 *
 *  Created on: 2019Äê11ÔÂ13ÈÕ
 *      Author: Administrator
 */

#ifndef UTIL_H_
#define UTIL_H_

#include<stdio.h>
#include<stdlib.h>
#include<math.h>

#define SUCCESS 0
#define FAILURE 1

//layer specification: a weight matrix
typedef struct layer{
	int numFeatures; //the number of features of one input data
 	int numNodes; //the number of nodes of this layer
	int numTrainingData; //the number of training data
	double ** wMt; //the weights matrix of this layer connected with the previous layer
	double ** wMtPre; //the weights matrix of this layer connected with the previous layer before updated
	double ** nodesInput; //the multiplication of the weight matrix and the output values of the previous layer
	double ** nodesOutput; //the output of each element of nodesInput feed into the activation function
	double ** gradientMt; //matrix of the gradient values which need to update the weights
}layer_t;

//swap training data
void swapTD(double **, int, int);

//swap label
void swapLabel(double *, int, int);

//shuffle the training data and its corresponding label
void shuffle(double **, double *, int);

//assign label according to training data
double assignLable(double, double);

//generate random training data
void generateTrainingData(int, double **, double *);

//initialize the value of weights
void iniWeights(layer_t *, int, int);

//initialize a layer
layer_t iniLayer(int, int, int);

//calculate vec product of weights and features
double vecProduct(double *, double **, int, int);

//activation function: the sigmod function
double sigmod(double);

//forward propogation for fully connected layer
void fpfullyConnected(double **input, layer_t *, int);

//set the values of a matrix equals the
void setValuesMt(layer_t *);

//backward propogation for output layer
void bpOutput(double *, layer_t *, layer_t *, double);

//backward propogation for hidden layer
void bpInput(double **, layer_t *, layer_t *, double *, float);

//initialize a double array
double *iniArr(int);

//print the array
void printArr(double *, int);

//initialize a double matrix
double **iniMt(int, int);

//free an array
void freeArr(double *);

//free a matrix
void freeMt(double **, int);

//free the "malloc" array and matrix of a layer
void cleanLayerMem(layer_t);

//for test results
void printMt(double **, int, int, int);

#endif /* UTIL_H_ */
