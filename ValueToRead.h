// bitmaps for errors word
#define BLESVR_ERR_DEVNOTFOUND (1)
#define BLESVR_ERR_CONNECTFAIL (2)

class ValueToRead
{
  public:
  ValueToRead();
  
  char* toString();

  char* set(char* s);           // return "OK" or error message
  char valueTag[64];            // tag name to identify the measurement value for logging
  char deviceId[40];            // ascii name or MAC address
  char serviceUuid[40];         // UUID of service to read value from
  char characteristicUuid[40];  // UUID of characteristic to read
  char deviceAddr[20];          // device MAC address xx:xx:xx:xx:xx:xx
  long minutesBetweenReads;     // number of minutes between reads (1..60*24)
  int connects;                 // 0 means never been connected
  int errors;                   // 0 means no errors - bit mask of errors encountered

  char tmpbuf[256];

    
};
