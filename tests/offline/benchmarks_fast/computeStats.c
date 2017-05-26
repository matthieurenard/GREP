#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define FILENUMBER			100
#define FILEPREFIX			"times"
#define MINFILENAME			"minTimes"
#define MAXFILENAME			"maxTimes"
#define MEANSFILENAME		"meansTime"

int main(int argc, char *argv[])
{
	char filename[50];
	char buffer[512];
	FILE *files[FILENUMBER];
	FILE *minsFile, *maxFile, *meansFile;
	int vals[FILENUMBER];
	int i, c, buffSize;
	int nLines;
	int min, max;
	long int sum;
	double mean;
	int ended = 0;
	
	for (i = 0 ; i < FILENUMBER ; i++)
	{
		sprintf(filename, "%s%d", FILEPREFIX, i+1);
		files[i] = fopen(filename, "r");
		if (files[i] == NULL)
		{
			perror("fopen");
			fprintf(stderr, "Error opening %s.\n", filename);
			exit(EXIT_FAILURE);
		}
	}

	minsFile = fopen(MINFILENAME, "w");
	if (minsFile == NULL)
	{
		perror("open minsFile");
		exit(EXIT_FAILURE);
	}

	maxFile = fopen(MAXFILENAME, "w");
	if (maxFile == NULL)
	{
		perror("open maxFile");
		exit(EXIT_FAILURE);
	}

	meansFile = fopen(MEANSFILENAME, "w");
	if (meansFile == NULL)
	{
		perror("open meansFile");
		exit(EXIT_FAILURE);
	}

	nLines = 0;
	while ((c = fgetc(files[0])) != EOF)
	{
		if (c == '\n')
		{
			nLines++;
			while ((c = fgetc(files[0])) == '\n');
			if (c == EOF)
				nLines--;
		}
	}
	nLines++;
	rewind(files[0]);

	do
	{
		for (i = 0 ; i < FILENUMBER ; i++)
		{
			buffSize = 0;
			while ((c = fgetc(files[i])) != EOF && (c < '0' || c > '9'))
			{
				if (c == '\n')
				{
					if (i == 0)
					{
						fprintf(minsFile, "\n");
						fprintf(maxFile, "\n");
						fprintf(meansFile, "\n");
					}
					while ((c = fgetc(files[i])) != EOF && (c < '0' || c > '9'));
					if (c == EOF)
					{
						ended = 1;
						break;
					}
				}
			}
			if (c != EOF)
				ungetc(c, files[i]);

			while ((c = fgetc(files[i])) >= '0' && c <= '9')
			{
				buffer[buffSize++] = (char)c;
			}
			if (c != EOF)
				ungetc(c, files[i]);
			buffer[buffSize] = '\0';
			vals[i] = atoi(buffer);
		}

		if (!ended)
		{
			min = vals[0];
			max = vals[0];
			sum = vals[0];
			for (i = 1 ; i < FILENUMBER ; i++)
			{
				min = (vals[i] < min) ? vals[i] : min;
				max = (vals[i] > max) ? vals[i] : max;
				sum += vals[i];
			}
			mean = (double)sum / FILENUMBER;
			fprintf(minsFile, "%d ", min);
			fprintf(maxFile, "%d ", max);
			fprintf(meansFile, "%d ", (int)round(mean));
		}
	} while (c != EOF && !ended);

	for (i = 0 ; i < FILENUMBER ; i++)
	{
		fclose(files[i]);
	}

	fclose(minsFile);
	fclose(maxFile);
	fclose(meansFile);

	return EXIT_SUCCESS;
}

