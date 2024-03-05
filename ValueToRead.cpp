#include "ValueToRead.h"
#include "ESP.h"

// constructor
ValueToRead::ValueToRead()
{
  valueTag[0] = '\0';
  deviceId[0] = '\0';
  serviceUuid[0] = '\0';
  characteristicUuid[0] = '\0';
  minutesBetweenReads = 60;
  deviceAddr[0] = '\0'; // set to non-null MAC addr only when we've seen it advertising!
  connects = 0;
  errors = 0;
}

// convert to string for diagnostic purposes
char* ValueToRead::toString()
{
  sprintf(tmpbuf,"%s,%d,%s,%s,%s,%s,%d,%x",
    valueTag,
    minutesBetweenReads,
    deviceId,
    serviceUuid,
    characteristicUuid,
    deviceAddr,
    connects,
    errors
    );
  return tmpbuf;
}

char* ValueToRead::set(char* s)
{
  // from a comma separated input string, set the values
  // valueTag,minutes,deviceId,serviceUuid,characteristicUuid
  char buf[64];
  int chloc = 0;
  int commaLocs[4];
  for (int i = 0; i < 4; i++) commaLocs[i] = -1;
  int foundEos = false;

  for (int i = 0; i < 4; i++)
  {
    while (!foundEos)
    {
      char c = s[chloc++];
      foundEos = (c == '\0');
      if (c == ',')
      {
        commaLocs[i] = chloc-1;
        break;
      }
    }
  }
  char* p = s;
  // now we know where all the commas are
  // -- get the valueTag
  int c0loc = commaLocs[0];
  if (c0loc < 0) return "format error, 1st comma not found";
  s[c0loc] = '\0';
  strncpy(valueTag,p,63);
  p = &s[c0loc+1];
  // -- get minutes between reads as an integer
  int c1loc = commaLocs[1];
  if (c1loc < 0) return "format error, 2nd comma not found";
  s[c1loc] = '\0';
  minutesBetweenReads = atoi(p); // first value is integer minutes between reads
  if (minutesBetweenReads < 1) minutesBetweenReads = 1;
  if (minutesBetweenReads > 1440) minutesBetweenReads = 1440; // once per day
  p = &s[c1loc+1];
  // -- get deviceId
  int c2loc = commaLocs[2];
  if (c2loc < 0) return "format error, 3rd comma not found";
  s[c2loc] = '\0';
  strncpy(deviceId,p,37);
  p = &s[c2loc+1];    
  // -- get service uuid
  int c3loc = commaLocs[3];
  if (c3loc < 0) return "format error, 4th comma not found";
  s[c3loc] = '\0';
  strncpy(serviceUuid,p,37);
  p = &s[c3loc+1];
  // -- get characteristic uuid
  strncpy(characteristicUuid,p,37);
  
  return "OK";
}
