/*
 * util.c
 *
 *  Created on: 2019Äê11ÔÂ11ÈÕ
 *      Author: Luyuan Zeng
 */

#include "util.h"

//swap training data
void swapTD(double **mt, int i, int j)
{
	double * temp = mt[i];
	mt[i]=mt[j];
	mt[j]=temp;

	return;
}

//swap label
void swapLabel(double *arr, int i, int j)
{
	int temp = arr[i];
	arr[i] = arr[j];
	arr[j] = temp;

	return;
}

//shuffle the training data and its corresponding label
void shuffle(double **trainingDT, double *label, int size)
{
	int i,j;
	for(i=0;i<size;i++){
		j=rand()%size;
		if(i!=j){
			swapTD(trainingDT, i, j);
			swapLabel(label, i, j);
		}
	}

	return;
}

//initialize the value of weights
void iniWeights(layer_t *l, int r, int c)
{
	for(int i=0; i<r; i++){
		for(int j=1; j<c; j++){
			//let the bias be 0;
			//other weights a random number between 0.01-0.2
			l->wMt[i][j]=(rand()%20+1)/100.0;
		}
	}

	return;
}

//initialize a layer
layer_t iniLayer(int r,  int c, int numTrainingDT)
{
	layer_t l;

	l.numFeatures=r;
	l.numNodes=c;
	l.numTrainingData=numTrainingDT;

	l.wMt=iniMt(r, c);
	l.wMtPre=iniMt(r,c);
	iniWeights(&l,r,c);
	l.nodesInput=iniMt(numTrainingDT,c);
	l.nodesOutput=iniMt(numTrainingDT,c);
	l.gradientMt=iniMt(r,c);

	return l;

}

//calculate the vector product of weights and features
double vecProduct(double *x, double ** w, int j, int size)
{
	double result=0;

	//printf("----------\n");
	for(int i=0;i<size;i++){
		result+=w[i][j]*x[i];
		//printf("%f*%f=%f\t",w[i][j],x[i],w[i][j]*x[i]);
	}

	//printf("result=%f\n",result);
	//printf("----------\n");

	return result;
}


//calcualte the sigmod function
double sigmod(double x)
{
	//activate: function sigmod
	//printf("sigmod activation=%f\n",1 / (1 + exp(-x)));
	return (1 / (1 + exp(-x)));
}

//forward propogation for fully connected layer
void fpfullyConnected(double **input, layer_t *ly, int isOutput)
{
	int i,j;

	for(i=0;i<ly->numTrainingData;i++){
		for(j=0;j<ly->numNodes;j++){
			ly->nodesInput[i][j]=vecProduct(input[i],ly->wMt,j,ly->numFeatures);
			ly->nodesOutput[i][j]=sigmod(ly->nodesInput[i][j]);
		}
	}

	//printMt(ly->nodesOutput, ly->numTrainingData, ly->numNodes, isOutput);

	return;
}

//set the values of a matrix equals the
void setValuesMt(layer_t * l)
{
	int r, c, i, j;

	r=l->numFeatures;
	c=l->numNodes;

	for(i=0; i<r; i++){
		for(j=0; j<c; j++){
			l->wMtPre[i][j]=l->wMt[i][j];
		}
	}

	return;
}


//backward propogation for output layer
void bpOutput(double *label, layer_t *inputLayer, layer_t *outputLayer, double learningRate)
{
	int m, i, j;
	float loss=0.0;//for test purpose

	setValuesMt(outputLayer);

	/*
	//print for testing
	printf("\n-------------------------------\n");
	printf("input:");
	printMt(inputLayer->nodesOutput,outputLayer->numTrainingData,outputLayer->numFeatures,0);
	printf("\n-------------------------------\n");
	printf("previous weights:");
	printMt(outputLayer->wMtPre,outputLayer->numFeatures, outputLayer->numNodes,0);
	printf("\n-------------------------------\n");
	printf("\n-------------------------------\n");
	printf("weights:");
	printMt(outputLayer->wMt,outputLayer->numFeatures, outputLayer->numNodes,0);
	printf("\n-------------------------------\n");
	printf("nodesInput:");
	printMt(outputLayer->nodesInput,outputLayer->numTrainingData,outputLayer->numNodes,0);
	printf("\n-------------------------------\n");
	printf("nodesOutput:");
	printMt(outputLayer->nodesOutput,outputLayer->numTrainingData,outputLayer->numNodes,0);
	printf("\n-------------------------------\n");
	printf("label data\n");
	printArr(label,outputLayer->numTrainingData);
	printf("\n-------------------------------\n");
	*/

	//apply stochastic gradient descent(SGD) here
	for(i=0; i<outputLayer->numFeatures; i++){
		for(j=0;j<outputLayer->numNodes;j++){
			for(m=0; m<outputLayer->numTrainingData; m++){
				//loss function 1/2*(y-y_label)^2 (for regression purpose)
				//outputLayer->gradientMt[i][j]=(label[m]-outputLayer->nodesOutput[m][j])*(outputLayer->nodesInput[m][j])*(1-outputLayer->nodesInput[m][j])*inputLayer->nodesOutput[m][i];
				//loss function cross entropy
				if(label[m]==0){
					loss+=((1-label[m])*log(1-outputLayer->nodesOutput[m][j]));
				}else{
					loss+=(label[m]*log(outputLayer->nodesOutput[m][j]));
				}

				outputLayer->gradientMt[i][j]+=(outputLayer->nodesOutput[m][j]-label[m])*inputLayer->nodesOutput[m][i];
			}
			outputLayer->wMt[i][j]=outputLayer->wMt[i][j]-learningRate*(outputLayer->gradientMt[i][j])/(outputLayer->numTrainingData);
		}
	}

	//printf("loss=%f\n",-loss/outputLayer->numTrainingData);

	/*
	//print for testing
	printf("\n-------------------------------\n");
	printf("gradientLH\n");
	printMt(outputLayer->gradientMt,outputLayer->numFeatures, outputLayer->numNodes, 0);
	printf("\n-------------------------------\n");
	printf("weights updated:");
	printMt(outputLayer->wMt,outputLayer->numFeatures, outputLayer->numNodes, 0);
	*/

	return;
}

//backward propogation for hidden layer
void bpInput(double **input, layer_t * inputLayer, layer_t * outputLayer, double *label, float learningRate)
{
	int m, n, i, j;

	//apply stochastic gradient descent(SGD) here
	for(i=0;i<inputLayer->numFeatures;i++){
		for(j=0;j<inputLayer->numNodes;j++){
			for(n=0;n<outputLayer->numNodes;n++){
				for(m=0; m<outputLayer->numTrainingData; m++){
					inputLayer->gradientMt[i][j]+=(outputLayer->nodesOutput[m][n]-label[m])*(outputLayer->wMtPre[j][n])*(inputLayer->nodesOutput[m][j])*(1-inputLayer->nodesOutput[m][j])*input[m][i];
					//printf("outputLayer->nodesOutput[m][n]=%f\n",outputLayer->nodesOutput[m][n]);
					//printf("outputLayer->nodesInput[m][n]=%f\n",outputLayer->nodesInput[m][n]);
					//printf("outputLayer->wMt[j][n]=%f\n",outputLayer->wMt[j][n]);
					//printf("inputLayer->nodesInput[m][j]=%f\n",inputLayer->nodesInput[m][j]);
					//printf("input[m][i]=%f\n",input[m][i]);
				}
				inputLayer->wMt[i][j]=inputLayer->wMt[i][j]-learningRate*inputLayer->gradientMt[i][j]/outputLayer->numTrainingData;
			}
		}
	}


	/*
	//print for testing
	printf("\n-------------------------------\n");
	printf("gradientLH\n");
	printMt(inputLayer->gradientMt,inputLayer->numFeatures, inputLayer->numNodes,0);
	printf("\n-------------------------------\n");
	printf("weights updated:");
	printMt(inputLayer->wMt,inputLayer->numFeatures, inputLayer->numNodes,0);
	*/

	return;
}

//initialize a double array
double *iniArr(int size)
{
	double *arr=(double *)malloc(size*sizeof(double));
	if(arr==NULL){
		printf("malloc double array failure\n");
		exit(FAILURE);
	}

	for(int i=0;i<size;i++){
		arr[i]=0.0;
	}

	return arr;
}

//initialize a double matrix
double ** iniMt(int r, int c)
{
	double **mt = (double **)malloc(r*sizeof(double *));

	if(mt==NULL){
		printf("malloc matrix failure\n");
		exit(FAILURE);
	}

	for(int i=0;i<r;i++){
		mt[i]=iniArr(c);
	}

	return mt;
}

//free an array
void freeArr(double *arr)
{
	free(arr);
}

//free a matrix
void freeMt(double ** Mt, int numRows)
{
	for(int i=0;i<numRows;i++){
		freeArr(Mt[i]);
	}

	free(Mt);

	return;
}

//free the "malloc" array and matrix of a layer
void cleanLayerMem(layer_t l)
{
	freeMt(l.wMt,l.numFeatures);
	freeMt(l.nodesInput,l.numTrainingData);
	freeMt(l.nodesOutput,l.numTrainingData);

	return;
}

//print the array
void printArr(double *arr, int size)
{
	for(int i=0; i<size; i++){
		printf("%f\t",arr[i]);
	}

	return;
}


//print the matrix
void printMt(double ** Mt, int r, int c, int printOutput)
{
	printf("----------\n");
	for(int i=0; i<r; i++){
		for(int j=0; j<c; j++){
			if(printOutput==0){
				printf("%f\t", Mt[i][j]);
			}else{
				if(Mt[i][j]>0.5){
					printf("1.0\t");
				}else if(Mt[i][j]==0.5){
					printf("0.5\t");
				}else{
					printf("0\t");
				}
			}
		}
		printf("\n");
	}
	printf("----------\n");

	return;
}
