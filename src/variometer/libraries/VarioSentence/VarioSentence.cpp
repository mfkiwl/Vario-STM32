// VarioSentence.cpp
//

#include <Arduino.h>

#include "VarioSentence.h"
//#include "LxNavSentence.h"
//#include "Lk8Sentence.h"


/////////////////////////////////////////////////////////////////////////////
// class VarioSentence

VarioSentence::VarioSentence(char type) : sentenceType(type)
{
	varioSentence = ((sentenceType == USE_LK8_SENTENCE) ?
				(IVarioSentence *)&LK8 : (IVarioSentence *)&LxNav);
				
	lastTick = millis();
}
	
void VarioSentence::begin(double height, double vel, double temp, double bat)
{
	varioSentence->begin(height, vel, temp, bat);
}
	
int VarioSentence::available()
{
	return varioSentence->available();
}
	
int VarioSentence::read()
{
	return varioSentence->read();
}

int VarioSentence::checkInterval()
{
	if ((millis() - lastTick) > VARIOMETER_SENTENCE_DELAY)
	{
		lastTick = millis();
		
		return true;
	}
	
	return false;
}