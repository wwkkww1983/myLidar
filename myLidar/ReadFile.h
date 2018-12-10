#pragma once

#include <iostream>
#include "WaveData.h"
#include "DeepWave.h"
using namespace std;

class ReadFile
{
public:
	ReadFile();
	~ReadFile();
	bool setFilename(char filename[100]);
	void readBlueAll();
	void readGreenAll();
	void readMix();
	void outputData();
	void readDeep();
private:
	char *m_filename;
	FILE *m_filePtr;
};