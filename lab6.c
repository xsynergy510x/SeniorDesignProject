#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "neural_network_float.h"		// Neural network 
//#include "neural_network_double.h"	// Neural network 


volatile int * oStart			= (int *) 0xFF200080;

volatile int * oClock			= (int *) 0xFF200010;		// Increments counter register in verilog

volatile int * iImgData			= (int *) 0xFF200060;
volatile int * iRowData			= (int *) 0xFF200070;
volatile int * iColData			= (int *) 0xFF200000;

volatile int * oRowAddr			= (int *) 0xFF200050;
volatile int * oColAddr			= (int *) 0xFF200020;

volatile int * oState			= (int *) 0xFF200030;		// Used to show the state with LEDs
volatile int * oDigits			= (int *) 0xFF200040;		// Displays proposed digits to HEX modules

void Clock(void)
{
	*oClock = 0;
	*oClock = 1;
}

int myPow(int n)
{
	int x[9] = {1, 10, 100, 1000, 10000, 100000, 10000000, 100000000, 1000000000};
	return x[n];
}

int myMod(int n)
{
	int x[11] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0};
	return x[n];
}

float mySigmoid(float x)
{
	if (x > 5) 
		return 0.001;
	else if (x < -5) 
		return 0.999;
	else 
		return 1/(1 + exp(x));
}

static inline unsigned int getCycles ()
{
  unsigned int cycleCount;
  // Read CCNT register
  asm volatile ("MRC p15, 0, %0, c9, c13, 0\t\n": "=r"(cycleCount));  
  return cycleCount;
}
static inline void initCounters ()
{
  // Enable user access to performance counter
  asm volatile ("MCR p15, 0, %0, C9, C14, 0\t\n" :: "r"(1));
  // Reset all counters to zero
  int MCRP15ResetAll = 23; 
  asm volatile ("MCR p15, 0, %0, c9, c12, 0\t\n" :: "r"(MCRP15ResetAll));  
  // Enable all counters:  
  asm volatile ("MCR p15, 0, %0, c9, c12, 1\t\n" :: "r"(0x8000000f));  
  // Disable counter interrupts
  asm volatile ("MCR p15, 0, %0, C9, C14, 2\t\n" :: "r"(0x8000000f));
  // Clear overflows:
  asm volatile ("MCR p15, 0, %0, c9, c12, 3\t\n" :: "r"(0x8000000f));
}

int main(void){
	
	// -----------------------------------------------------------------------------------
	// 
	// Variables
	//
	// -----------------------------------------------------------------------------------
	
	// Image variables
	int imgArr[480][640];				// Array that holds image
	
	// We use a buffer of 10% on each side
	int colsMin		= 64; 		// 640*10%
	int colsMax		= 576; 		// 640 - 640*10%
	int rowsMin		= 48;		// 480*10%
	int rowsMax		= 432; 		// 480 - 480*10%
	
	// Loop indexes
	int rows, cols;					
	int i, j, k, x, y;

	// ROI variables
	int rowsSumArr[480] = { 0 };	// Holds summation of all rows
	int colsSumArr[640] = { 0 };	// Holds summation of all cols
	
	int rowsSumMax = 0;			// These holds values for the light level
	int colsSumMax = 0;
	int rowsLevel = 0;
	int colsLevel = 0;
	
	int binaryRowsSumArr[480] = { 0 };	// We transform these values into binary values
	int binaryColsSumArr[640] = { 0 };	//    according to the calculated level
	
	int projTop = 0, projBottom = 0;	// Row/col indexes for projector space
	int projLeft = 0, projRight = 0;
	
	int roiTop = 0, roiBottom = 0;		// Row/col indexes for ROI
	int roiLeft = 0, roiRight = 0;
	
	// Segmentation and Resize variables
	int numDigits = 0;
	int currentDigit = 0;
	int digitWidth = 0;
	int digitHeight = 0;
	int segmentIntensity = 0;			// Accumulator to check if the segment isn't all white pixels
	int digitArr[784] = { 0 };
	//int digitArr[400] = { 0 };
	
	
	// Neural network variables
	float sum;
	float Z1[200];
	float Z2[200];
	int max = 0;
	int pos = 0;
	
	int answer = 0;
	
	// Timing variables
	int RECORD_TIME = 0;				// Variable that decides if we print (1) or not (0)
	
	// -----------------------------------------------------------------------------------
	// 
	// Begin system
	//
	// -----------------------------------------------------------------------------------
	printf("System restart\n");
	*oStart = 1;				// Initiate clock system
	*oClock = 0;				// Set HPS simulated clock to 0
	*oDigits = 0;

	initCounters(); 
	volatile unsigned int time;

	while(1)
	{
		// -----------------------------------------------------------------------------------
		// 
		// Reset the variables for next iteration
		//
		// -----------------------------------------------------------------------------------
		//for (i = 0; i < 640; i++) colsSumArr[i] = 0;
		//for (i = 0; i < 480; i++) rowsSumArr[i] = 0;
		
		memset(colsSumArr, 0, sizeof(colsSumArr));
		memset(rowsSumArr, 0, sizeof(rowsSumArr));
		
		max = -100000000;
		answer = 0;
		
		rowsSumMax = 0;
		colsSumMax = 0;
		
		// -----------------------------------------------------------------------------------
		// 
		// Prompt user to begin
		//
		// -----------------------------------------------------------------------------------
		*oState = 1;				// State 1 - Ready
			
		if (RECORD_TIME != 2)
		{
			printf("Enter (2) for infinite loop, (1) for timing run, (0) just to run: ");
			scanf("%d", &RECORD_TIME);
		}
		
		switch (RECORD_TIME)
		{
			case (0):	break;
			case (1):	break;
			case (2):	break;
			default:	RECORD_TIME = 0;
		}
		
		*oStart = 0;				// Stop camera, begin system
		if (RECORD_TIME) time = getCycles();
		
		// -----------------------------------------------------------------------------------
		// 
		// Read SDRAM -> imgArr[480][640] array of 1's and 0's
		//	The image is made either 255 or 0 in the verilog code
		//  Convert to 0's and 1's in C
		//
		// -----------------------------------------------------------------------------------
		
		*oState = 3;				// State 2 - Reading image
			
		for (rows = 0; rows < 480; rows++)	// 640x480
		{	
			for(cols = 0; cols < 640; cols++)
			{
				Clock();
				imgArr[rows][cols] = *iImgData;		// 0's and 1's are determined in verilog
			}
		}
				
		// Restart Clock because we're done with the SDRAM
		*oStart = 1;
		
		// DEBUG - Print out entire image array with buffer
		/*
		printf("Printing out full image array with buffers\n");
		for (rows = 48; rows < 432; rows++)	// 640x480
		{
			for(cols = 64; cols < 576; cols++)
			{					// Get current value of counter[1]		
				if (imgArr[rows][cols])
					printf(" ");
				else
					printf("0");
			}
			printf("\n");
		}
		//*/
		
		// -----------------------------------------------------------------------------------
		// 
		// Detect white projector space
		//	Put a 10% buffer on each side:	cols = 64 -> 576 : imgLeftSide -> imgRightSide
		//									rows = 48 -> 432 : imgTopSide -> imgBotSide
		//
		// -----------------------------------------------------------------------------------
		*oState = 7;				// State 3 - Detect projector
				
		// Sum up rows and columns
		for (cols = colsMin; cols < colsMax; cols++)
		{
			for (rows = rowsMin; rows < rowsMax; rows++)
			{
				colsSumArr[cols] += imgArr[rows][cols];
				rowsSumArr[rows] += imgArr[rows][cols];
			}
		}
		
		// Find max value of rows sum
		for (rows = 0; rows < 480; rows++)
		{
			if (rowsSumArr[rows] > rowsSumMax)
				rowsSumMax = rowsSumArr[rows];
		}
		
		// Calculate rows level - ~10% less than the max
		rowsLevel = rowsSumMax - (rowsSumMax >> 3);
		
		// Find max value of cols sum
		for (cols = 0; cols < 640; cols++)
		{
			if (colsSumArr[cols] > colsSumMax)
				colsSumMax = colsSumArr[cols];
		}
		
		// Calculate cols level - ~5% less than the max
		colsLevel = colsSumMax - (colsSumMax >> 4);
		
		// Generate the array of binary values for rows and cols
		for (rows = rowsMin; rows < rowsMax; rows++)
		{
			if (rowsSumArr[rows] > rowsLevel)
				binaryRowsSumArr[rows] = 1;
			else
				binaryRowsSumArr[rows] = 0;
		}
		for (cols = colsMin; cols < colsMax; cols++)
		{
			if (colsSumArr[cols] > colsLevel)
				binaryColsSumArr[cols] = 1;
			else
				binaryColsSumArr[cols] = 0;
		}
		
		// Scan for left of white projector space
		for (cols = colsMin; cols < colsMax; cols++)
		{
			if (binaryColsSumArr[cols] == 1)
			{
				projLeft = cols;
				break;
			}
		}
		
		// Scan for right of white projector space
		for (cols = colsMax - 1; cols > colsMin - 1; cols--)
		{
			if (binaryColsSumArr[cols] == 1)
			{
				projRight = cols;
				break;
			}
		}
		
		// Scan for top side of white projector space
		for (rows = rowsMin; rows < rowsMax; rows++)
		{
			if (binaryRowsSumArr[rows] == 1)
			{
				projTop = rows;
				break;
			}			
		}

		// Scan for bottom side of white projector space
		for (rows = rowsMax - 1; rows > rowsMin - 1; rows--)
		{
			if (binaryRowsSumArr[rows] == 1)
			{
				projBottom = rows;
				break;
			}
		}		
		
		/*
		// DEBUG - Print out the binary arrays
		printf("rows: \n");
		for (rows = rowsMin; rows < rowsMax; rows++)
		{
			if (binaryRowsSumArr[rows])
				printf(".");
			else
				printf("0");
		}
		printf("\ncols: \n");
		for (cols = colsMin; cols < colsMax; cols++)
		{
			if (binaryColsSumArr[cols])
				printf(".");
			else
				printf("0");
		}
		printf("\n");
		*/
		
		/*
		// DEBUG - Print white projector space
		printf("projLeft: %d, projRight: %d\n", projLeft, projRight);
		printf("projTop: %d, projBottom: %d\n", projTop, projBottom);
		
		for (rows = projTop; rows < projBottom; rows++)
		{
			for (cols = projLeft; cols < projRight; cols++)
			{
				if (imgArr[rows][cols])
					printf(" ");
				else
					printf("0");
			}
			printf("\n");
		}
		*/
		
		// -----------------------------------------------------------------------------------
		// 
		// Detect ROI black space
		//	Using the same methods as detecting the white projector space
		//
		// -----------------------------------------------------------------------------------
		*oState = 15;				// State 4 - Detect ROI
		
		// Calculate 10% buffer on projector space
		projTop = projTop + (projTop >> 3);
		projBottom = projBottom - (projBottom >> 3);
		projLeft = projLeft + (projLeft >> 3);
		projRight = projRight - (projRight >> 3);
		
		// Scan for top of ROI
		for (rows = projTop; rows < projBottom; rows++)
		{
			if (binaryRowsSumArr[rows] == 0)
			{
				roiTop = rows;
				break;
			}
		}
		
		// Scan for bottom of ROI
		for (rows = projBottom - 1; rows > (projTop - 1); rows--)
		{
			if (binaryRowsSumArr[rows] == 0)
			{
				roiBottom = rows;
				break;
			}
		}
		
		// Scan for left of ROI
		for (cols = projLeft; cols < projRight; cols++)
		{
			if (binaryColsSumArr[cols] == 0)
			{
				roiLeft = cols;
				break;
			}
		}
		
		for (cols = projRight - 1; cols > (projLeft - 1); cols--)
		{
			if (binaryColsSumArr[cols] == 0)
			{
				roiRight = cols;
				break;
			}
		}
	
		/*
		// DEBUG - Print ROI
		printf("projLeft: %d, projRight: %d\n", projLeft, projRight);
		printf("projTop: %d, projBottom: %d\n", projTop, projBottom);
		
		for (rows = roiTop; rows < roiBottom; rows++)
		{
			for (cols = roiLeft; cols < roiRight; cols++)
			{
				if (imgArr[rows][cols])
					printf(" ");
				else
					printf("0");
			}
			printf("\n");
		}
		*/
		
		// -----------------------------------------------------------------------------------
		// 
		// Segmentation loop
		//	Iterate through the digits of the ROI
		//	# of digits ~= ROI width / ROI height
		//	
		//
		// -----------------------------------------------------------------------------------
		*oState = 31;				// State 5 - Segmentation
				
		numDigits = round((roiRight - roiLeft)*1.0/(roiBottom - roiTop));
		numDigits = min(numDigits, 5);
		digitWidth = (roiRight - roiLeft) / numDigits;
		digitHeight = roiBottom - roiTop;
				
		for (currentDigit = 0; currentDigit < numDigits; currentDigit++)
		{
			// -----------------------------------------------------------------------------------
			// 
			// Resize the segmented digit
			//
			// -----------------------------------------------------------------------------------
						
			segmentIntensity = 0;
			// Create a 28x28 by sampling every 1/28th of the ROI
			for (i = 0; i < 28; i++)
			{
				for (j = 0; j < 28; j++)
				{
					x = round(i*(digitHeight - 1) / 27);
					y = round(j*(digitWidth - 1) / 27);
					
					// X -> Height, doesn't change
					// Y -> Width, the index changes as we move across the ROI
					digitArr[i + j * 28] = imgArr[roiTop + x][roiLeft + currentDigit*digitWidth + y];
					
					// Try to see if image is mainly whitespace
					segmentIntensity += digitArr[i + j * 28];
					if (segmentIntensity > 275)
						goto skip_digit;
				}
			}
						
			// -----------------------------------------------------------------------------------
			// 
			// Check if segment isn't 75%+ white pixels
			//	It might be a bad segment, so skip
			//
			// -----------------------------------------------------------------------------------
			
			/*
			// DEBUG - Print out the 28x28 matrix
			printf("Print 28x28\n");
			for (i = 0; i<28; i++)
			{
				for (j = 0; j<28; j++)
				{
					if (digitArr[i + j*28])
						printf(" ");
					else
						printf("0");
				}
				printf("\n");
			}
			*/
			
			// -----------------------------------------------------------------------------------
			// 
			// Send 784x1 to Neural Network
			//
			// -----------------------------------------------------------------------------------
			*oState = (*oState << 1) + 1;
						
			// Level 1 Weight and bias + sigmoid function
			for (i = 0; i < 200; i++) 
			{
					sum = 0;
					for (k = 0; k < 784; k++)
					{
						//sum += W1[i][k] * digitArr[k];
						if (digitArr[k])
							sum += W1[i][k];
					}			
					Z1[i] = 1/(1 + exp(-1*(sum + B1[i])));
					//Z1[i] = mySigmoid(-1*(sum + B1[i]));
			}
			
			// Level 2 Weight and bias + sigmoid
			for (i = 0; i < 200; i++) 
			{
					sum = 0;
					for (k = 0; k < 200; k++) 
					{
						sum += W2[i][k] * Z1[k];
					}
					Z2[i] = 1 / (1 + exp(-1*(sum + B2[i]))) ;
					//Z2[i] = mySigmoid(-1*(sum + B2[i]));
			}
			
			// Level 3
			for (i = 0; i < 10; i++)
			{
					sum = 0;
					for (k = 0; k < 200; k++) 
					{
						sum += W3[i][k] * Z2[k];
					}
					
					if (sum > max)
					{
						max = sum;
						pos = i + 1;
					}
			}
			answer = answer + myPow(numDigits - (currentDigit + 1)) * myMod(pos);
			skip_digit: ;
		} // End for (currentDigit....
		
		*oDigits = answer;
		printf("Guess: %d\n", answer);
		if (RECORD_TIME) printf("Cycles: %d\n\n", getCycles() - time);
		
	} // While(1)
	
	return 0;
}