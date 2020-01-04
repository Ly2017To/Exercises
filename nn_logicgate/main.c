/*
 * main.c
 *
 *  Created on: 11.11.2019
 *      Author: Luyuan Zeng
 *      Reference1: http://www.briandolhansky.com/blog/category/neural%20networks
 *      Reference2: http://www.cs.bham.ac.uk/~jxb/INC/nn.html
 */

#include "util.h"

#define NUMTRAININGDATA 4 //the number of training data
#define NUMFEATURES 3 //the number of features of data
#define MAXITERATIONS 5000 //the maximum number of iterations
#define LEARNINGRATE 0.01 //the learning rate when update the weights

int main (void)
{
	//input data and its corresponding label
	//for input, the first element of each array is for multiply with bias weight
	double **trainingDataMt=iniMt(NUMTRAININGDATA,NUMFEATURES);
	//label of input data
	double *label=iniArr(NUMTRAININGDATA);

	//in this case, we use nn to simulate an XOR gate
	//just type it here. :p

	trainingDataMt[0][0]=1;
	trainingDataMt[0][1]=0;
	trainingDataMt[0][2]=0;
	label[0]=0;

	trainingDataMt[1][0]=1;
	trainingDataMt[1][1]=0;
	trainingDataMt[1][2]=1;
	label[1]=1;

	trainingDataMt[2][0]=1;
	trainingDataMt[2][1]=1;
	trainingDataMt[2][2]=0;
	label[2]=1;

	trainingDataMt[3][0]=1;
	trainingDataMt[3][1]=1;
	trainingDataMt[3][2]=1;
	label[3]=0;

	//Here: our neural network has one input layer and one output layer

	//declare the input layer
	layer_t inputLayer=iniLayer(NUMFEATURES,2,NUMTRAININGDATA);

	//declare the output layer
	layer_t outputLayer=iniLayer(2,1,NUMTRAININGDATA);

	for(int epoch=0; epoch<MAXITERATIONS; epoch++){
		shuffle(trainingDataMt, label, NUMTRAININGDATA);

		fpfullyConnected(trainingDataMt,&inputLayer,0);

		fpfullyConnected(inputLayer.nodesOutput,&outputLayer,1);

		bpOutput(label,&inputLayer,&outputLayer,LEARNINGRATE);

		bpInput(trainingDataMt,&inputLayer,&outputLayer,label,LEARNINGRATE);
	}


	//initialize the testing data
	double **testingDataMt=iniMt(1,NUMFEATURES);

	//the first testing data
	testingDataMt[0][0]=1;
	testingDataMt[0][1]=0.9;
	testingDataMt[0][2]=0.1;

	inputLayer.numTrainingData=1;

	fpfullyConnected(testingDataMt,&inputLayer,0);

	outputLayer.numTrainingData=1;

	fpfullyConnected(inputLayer.nodesOutput,&outputLayer,1);

	printMt(outputLayer.nodesOutput, outputLayer.numTrainingData, outputLayer.numNodes,1);


	cleanLayerMem(inputLayer);
	cleanLayerMem(outputLayer);

	return SUCCESS;
}
