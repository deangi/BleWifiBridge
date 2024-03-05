//---------------------------------------------------------------------------------
// Bluetooth Low Energy IoT Bridge
//
// Dean Gienger, February, 2024
//
// This will monitor some set of BTLE parameters and push them to a WiFi IoT Hub.
// In essense it bridges the parameters from some set of BTLE sensors and bridges
// them to an IoT hub which can take care of storing them and eventually forwarding 
// the IoT data to some central server.
//
// The IoT Hub is a project of it's own: https://github.com/deangi/IoTHub
//
// The hardware is based on the ESP32 platform and uses the built-in BTLE and
// WiFi modes to serve as a hub.
//
// A configuration file (/config.ini) has parameters to:
// - connect to the WiFi (ssid, password)
// - ip of the IoT Hub to bridge received BTLE data to
// - a list of BTLE devices to connect to and characteristics to read, each at it's own sampling period
//
//
// For example we may have a temperature sensor that we read hourly, a water sensor
// that we read every minute, etc.
//
// We reference a list of BTLE devices by their name and Bluetooth MAC ID
// We reference characteristics by their UUID and how often they should be read
//
//
// Components
// WiFi connection
// BTLE scanner driven by list of devices and characteristics
// Some kind of queue of when we're going to read the next data
// that can handle reading different parameters at different rates
//
// Sub components:
// - SPIFFS and support routines to read config file
// - Timer support routine (1/10'th of a second accuracy, 32 bit long - 21+ years before rollover)
// - Serial and logging support, notification of errors (can't read config file, etc)
//
//
// ESP32 board - not to particular - can be most any board - as they all support BTLE and WIFI
//    - Some boards will get a "brown out" when you use both BT and WiFi.
//   TTGO - seems to work.
//
// Partitioning - need a small partition for SPIFFS and storing the config.ini file - minimal SPIFFS
//
// https://randomnerdtutorials.com/esp32-bluetooth-low-energy-ble-arduino-ide/
// Scan for a client, then connect to it.
// https://github.com/nkolban/esp32-snippets/blob/master/Documentation/BLE%20C%2B%2B%20Guide.pdf
// https://github.com/nkolban/ESP32_BLE_Arduino/blob/master/src/BLEAdvertisedDevice.h
//
//
// Development Phases / TODO
// 1 - Get a BLE scan running *
// 2 - Find a particular device by UUID *
// 3 - Find a service *
// 4 - Query values from a service *
// 5 - Read config file *
// 6 - Read values from BLE servers based on config file values and schedule *
// 7 - connect to WiFi and send values to IoT Hub *
// 8 - rescan ble periodically *
// 9 - reconnect to WiFi as needed *
//10 - if send to IoT hub fails, buffer up and send later
//11 - get time from IoT hub periodically if we can't get time from NTP - low pri
//
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#include <SPI.h>
#include <SPIFFS.h>

#include <ESP32Time.h>          // RTC time functions
#include <WiFi.h>               // WiFi driver code
#include <WiFiUdp.h>            // UDP code for NTPClient support
#include <NTPClient.h>          // Network Time Protocol (NTP) client library

#include <esp_task_wdt.h>

#include <HTTPClient.h>         // connect to web server

#include "ValueToRead.h"
#include "StrQueue.h"

#define SIGNALLED (2)

// Error Codes
#define ERR_NOSPIFFS (1)
#define ERR_NOWIFI   (2)


#define SIGNALLED (2)
#define LEDON digitalWrite(SIGNALLED,HIGH)
#define LEDOFF digitalWrite(SIGNALLED,LOW)
int ledmode;

// watch dog timer timeout in seconds
#define WDT_TIMEOUT (300) 

// Queue to store values that didn't post to the server for some reason
#define QUEUESIZE (4096)
StrQueue measuredData(QUEUESIZE);

// Config file information
char wifissid[64];
char wifipwd[64];
char iothubip[64]; // ip address of hub
char btlebridgename[64]; // name of this hub in the BTLE universe

void println(char* s) { Serial.println(s); }
void println(String s) { Serial.println(s); }
void print(char* s) { Serial.print(s); }
void print(String s) { Serial.print(s); }

//-------------------------------------------------------------
// blink an error code
//-------------------------------------------------------------
void blink(int code)
{
  int i;
  
  if (code < 0) code = 0;
  if (code > 10) code = 10;
  
  int dly=100; // ms
  for (;;) // fatal error code, never return from this
  {
    // S
    for (i = 0; i < 3; i++)
    {
      LEDON; delay(dly);
      LEDOFF; delay(3*dly);
    }
    delay(3*dly);
    // O
    for (i = 0; i < 3; i++)
    {
      LEDON; delay(3*dly);
      LEDOFF; delay(3*dly);
    }
    delay(3*dly);
    // S
    for (i = 0; i < 3; i++)
    {
      LEDON; delay(dly);
      LEDOFF; delay(3*dly);
    }
    delay(9*dly);
    // Code
    for (int i = 0; i < code; i++)
    {
      LEDON; delay(2*dly);
      LEDOFF; delay(2*dly);    
    }
    delay(12*dly);
  }
}

//-----------------------------------------------------------------
// real time clock (software based, not backed up for power failures
// Subject to some drift!!!
ESP32Time rtc(0); 

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
int needNtp = true;

#define SIGNON "\nBTLE - WiFi IoT Bridge V1.2\n"

int wifiWaitCounter;
int wifiIsConnected;
String ipAddress;

//----------------------------------------------------------------------
// Connect to some WiFi access point as a station
int connectToWiFi()
{
 // Connect to Wi-Fi, 1=connected, 0=not connected
  int maxWaitTimeToConnect = 50; // seconds
  WiFi.begin(wifissid, wifipwd);
  wifiWaitCounter=0;
  wifiIsConnected=0;
  
  while (WiFi.status() != WL_CONNECTED) 
  {
    esp_task_wdt_reset(); // reset watch-dog timer
    delay(1000);
    println("Connecting to WiFi..");
    if (++wifiWaitCounter >= maxWaitTimeToConnect)
      break;
  }

  // Print ESP32 Local IP Address
  if (wifiWaitCounter >= maxWaitTimeToConnect)
  {
    wifiIsConnected = false;
    println("WiFi connect failed");  
  }
  else
  {
    ipAddress = IpAddress2String(WiFi.localIP());
    print("WiFi connected: "); println(ipAddress);
    wifiIsConnected=true;
  }
  return wifiIsConnected;
}


//----------------------------------------------------------------------
void getNtpTime()
{
  if (wifiIsConnected==0) return; // can't do it if no wifi connected.
  // NTP Client
  timeClient.begin();
  timeClient.setTimeOffset(0);
  int waitingForNtp = 10;
  while (waitingForNtp--)
  {
    if (!timeClient.update())
    {
      timeClient.forceUpdate();
      delay(500);
    }
    else
      break;
  }
  unsigned long epochTime = timeClient.getEpochTime();
  println("NTP Time: ");
  println(timeClient.getFormattedDate());
  rtc.setTime(epochTime);
}

//------------------------------------------------------------------------
// convert IP address to string
String IpAddress2String(const IPAddress& ipAddress)
{
  return String(ipAddress[0]) + String(".") +\
  String(ipAddress[1]) + String(".") +\
  String(ipAddress[2]) + String(".") +\
  String(ipAddress[3])  ; 
}

// UUIDs - these are long strings of hex digits with some hyphens
// 5 groups of hex digits separated by 5 hyphens (-) minus signs
// example: b182407f-845b-42e9-8e6f-a61af5bb23dd
// 8hex-4hex-4hex-4hex-12hex digits, 32 hex digits plus 4 hypens is 36 characters in length
//
// In the config file we need a list of devices to scan for
// For each device there will be one or more services (each services is a UUID)
// For each service there will be one or more characteristics (each characteristic is a UUID) to read a value from
// So a complete value to read is identified by  three pieces of data, 
//   device identifier (ascii name or MAC address)
//     service UUID within device
//       characteristic UUID within service
//
// The configuration file will need a list of one or more of these to scan on a schedule
// For each value read, we need the 3 id values, and a schedule, like hourly or every minute
//
#define BTLE_CLIENT_NAME "BTLE-WIFI-BRIDGE"

#define CONFIGFN "/config.ini"



int scanTime = 10; //In seconds
BLEScan* pBLEScan;
BLEClient * pClient;

//----------- from INI file - list of values to read ------------
#define MAXVALUES2READ (100)
int nValues; // number of values to read from BLE devices
ValueToRead * vals2read[MAXVALUES2READ];

//--------------------------------------------------
// Callback routine during scan when a BTLE advertiser is detected
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      ledmode ^= 1;
      digitalWrite(SIGNALLED,ledmode);
      //Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());
      // scan list of values to be read to see if there is a match
      // for the device name or device address
      // advertisedDevice.getName() or advertisedDevice.getAddress()
      std::string devName = advertisedDevice.getName();
      BLEAddress devAddr1 = advertisedDevice.getAddress();
      std::string devAddr = devAddr1.toString();
      //Serial.print((char*)devName.c_str()); Serial.print("="); 
      //Serial.println((char*)devAddr.c_str());
      
      esp_task_wdt_reset(); // reset watch-dog timer
      
      for (int i = 0; i < nValues; i++)
      {
        ValueToRead* p = vals2read[i];
        if ((p->deviceId == devName) || (p->deviceId == devAddr))
        {
          // found a match, save the address
          strncpy(p->deviceAddr,(char*)devAddr.c_str(), 18);
          Serial.print("Found '"); Serial.print((char*)devName.c_str()); 
          Serial.print("' at "); Serial.println((char*)devAddr.c_str());
        }
      }
    }
};

//----------------------------------------------------------
// read line from input text file
int readln(File finp, uint8_t* buf, int maxlen)
{
  // return true on successful read, false on EOF
  // 10 or 13 (LF, CR) or both are EOL indicators
  int len=0;
  int eof=false;

  buf[0]=0;
  while (len<(maxlen-1))
  {
    if (!finp.available())
    {
      eof=true;
      break;
    }
    char c = finp.read();
    if (c < 0) 
    {
      eof=true;
      break; // EOF
    }
    if (c==13) continue; // ignore CR
    if (c==10) break; // end-of-line
    buf[len++]=c;
  }
  buf[len]=0; // null terminate
  return !eof;
}

//----------------------------------------------------------
// retrieve a value for a key in the config file
void readKey(char* configFn, char* key, char* outbuf, int maxlen)
{
  outbuf[0] = 0; // returning null string on error 
  //
  // Config file is key=value format
  // SSID=mywifi
  // PASSWORD=mypassword
  // TIMEZONE=-8
  // OFFSET=123590 
  //
  // pass in key with trailing = sign!!! 
  // readKey("/test.cfg","MYKEY=", outbuf, 127);

  File finp = SPIFFS.open(configFn, FILE_READ);
  if (!finp)
  {
    print("Unable to read config file");
    return;
  }
  // scan file and look for key
  char buf[128];
  int n = strlen(key);
  while (readln(finp, (uint8_t*) buf, 127))
  {
    if (strncmp(buf,key,n) == 0) // found
    { 
      println(buf);
      strncpy(outbuf,&buf[n],maxlen);
      break;
    }
  }
  finp.close();
 
}


//------------- keeping track of minutes -----------
// The software implements a simple minute based scheduler.
// There is a counter that ticks at one minute intervals.
// Each value to be read has a schedule of how many minutes
// between each read, so read every minute, every 10 minutes
// every 60 minutes (hourly), every day (24*60 minutes) etc
//
// Each minute interval we scan the list of values to be
// read, and if the minute counter modulo the scheduled
// minutes between reads is 0, we schedule a read of that
// value.
//
int lastMin; // 0..59
unsigned long minuteCounter; // per minute
int timeForScan; // set to true when we should scan for BLE devices

//--------------------------------------------------
void setup() 
{
  char tmpbuf[256];
  char tmpname[32];

  //--- initialize the comfort (signal) LED
  pinMode(SIGNALLED, OUTPUT);
  ledmode=HIGH;
  // turn comfort LED on for the setup processing
  digitalWrite(SIGNALLED,ledmode);

  //--- initialize the RTC
  rtc.setTime(0,0,0,1,1,2024);

  //--- initialize the diagnostic serial port
  Serial.begin(115200);
  Serial.println(SIGNON);
    
  //--- initialize SPIFFS (file system)
  if(!SPIFFS.begin(true))
  {
    print("An Error has occurred while mounting SPIFFS");
    blink(ERR_NOSPIFFS);
    //return; what to do here?  We can't do much without the file system
  }
  
  //--- read the configuration file
  readKey(CONFIGFN,"WIFISSID=",wifissid,63);
  readKey(CONFIGFN,"WIFIPWD=",wifipwd,63);
  readKey(CONFIGFN,"IOTHUBADDR=",iothubip,63);
  readKey(CONFIGFN,"BLENAME=", btlebridgename, 63);
  // values to read
  nValues=0;
  for (int i = 1; i < MAXVALUES2READ; i++)
  {
    // VALUE1=15,11:22:33:44:55:66,b7972d95-e930-4144-beb0-6a6e8b9a3d23,20b5e09a-f998-47f0-aae3-4b361ebc8233
    // VALUEn=minutes,deviceId,ServiceUuid,CharacteristicUuid
    sprintf(tmpname,"VALUE%d=",i);
    readKey(CONFIGFN,tmpname, tmpbuf, 255); // try to read it
    if (tmpbuf[0] != '\0') // found "VALUEn="
    {
      // yes, found one, allocate and initialize an entry to read it and keep track of it
      Serial.print("\nParsing value: "); Serial.println(tmpbuf);
      vals2read[nValues] = new ValueToRead();
      char* result = vals2read[nValues]->set(tmpbuf);
      Serial.println(result);
      Serial.println(vals2read[nValues]->toString());
      nValues++;
    }
  }
  Serial.printf("%d values read from config file\n",nValues);
  
  //--- connect to WiFi
  if (!connectToWiFi()) blink(ERR_NOWIFI);
  
  //--- see if we can get date/time from WiFi - Network Time Protocol (NTP)
  getNtpTime();

  //--- initialize BLE client and start scanning for BLE advertising packets
  BLEDevice::init(btlebridgename);
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);  // less or equal setInterval value
  pClient = BLEDevice::createClient(); // create client to use for connecting, reading values

  //--- Initialize watch-dog-timer to reset the BLE bridge if it goes off line
  esp_task_wdt_init(WDT_TIMEOUT, true); // enable WDT so the ESP32 restarts after 10 seconds
  esp_task_wdt_add(NULL); // enable WDT for this task

  //--- initialize some application variables
  minuteCounter = -1;
  timeForScan = true;
  
  lastMin = rtc.getMinute();

  //--- LED off at end of setup
  LEDOFF;
}

//----------------------------------------------------------------------
// Completely explore a server - list all services and all characteristics

void serverExplorer(BLEClient * pClient, BLEAddress serverAddress)
{
  pClient->connect(serverAddress);
  if (!pClient->isConnected())
  {
    Serial.println("Connection failed");
  }
  else
  {
    Serial.println("Connected");
    std::map<std::string, BLERemoteService*>* services = pClient->getServices(); // get list of services
  
    //Serial.printf("Number of services found: %d\n",services->count);
    
    // print services
    int svcidx = 1;
    for (std::map<std::string, BLERemoteService*>::iterator it = services->begin(); it!=services->end();++it)
    {
      Serial.printf("Service %d\n", svcidx++);
      delay(200);
      std::string svcName = it->first;
      std::string svcSvid = it->second->toString();
      Serial.print(svcName.c_str()); 
      Serial.print("=");
      Serial.println(svcSvid.c_str());
      BLERemoteService* p = it->second;
      BLEUUID svcUUID = p->getUUID();
      //std::string value = p->getValue(p->getUUID());
      //Serial.println(value.c_str());
      // get characteristics
      std::map<std::__cxx11::basic_string<char>, BLERemoteCharacteristic*>* characteristics = p->getCharacteristics();
      for (std::map<std::__cxx11::basic_string<char>, BLERemoteCharacteristic*>::iterator itch = characteristics->begin(); itch != characteristics->end(); ++itch)
      {
        BLERemoteCharacteristic * rc = itch->second;
        BLEUUID charUUID = rc->getUUID();
        std::string cuuid = charUUID.toString();
        std::string val = p->getValue(charUUID);
        Serial.print('['); Serial.print(cuuid.c_str()); Serial.print("]="); Serial.println(val.c_str());
      }
    }
    
    pClient->disconnect();
  }
}

//--------------------------------------------------
// Read value from a BLE device
void readValue(BLEClient * pClient, ValueToRead* v2rd, char* buf, int maxlen)
{
  // connect to the server device
  BLEAddress serverAddr(v2rd->deviceAddr);
  pClient->connect(serverAddr);
  if (pClient->isConnected())
  {
    BLEUUID svcUuid(v2rd->serviceUuid);
    BLEUUID chrUuid(v2rd->characteristicUuid);
    // read the value
    std::string val = pClient->getValue(svcUuid, chrUuid);
    v2rd->connects++;
    strncpy(buf,(char*)val.c_str(), maxlen-1);
    //Serial.printf("Read '%s' from %s\n",(char*)val.c_str(), v2rd->toString());
    pClient->disconnect();
  }
  else
  {
    v2rd->errors |= BLESVR_ERR_CONNECTFAIL; // failed to connect
    Serial.printf("Connect failed: %s\n",v2rd->toString());
    strncpy(buf,"*UNKNOWN*",maxlen-1);
  }
}


String timeFromIotHub = "";
//--------------------------------------------------
int forwardValueToIotHub(char* msg)
{
  print("send to IoT Hub: "); println(msg);  char buf[256];
  sprintf(buf, "http://%s/?LOG=%s", iothubip, msg);
  Serial.println(buf);
  
  HTTPClient http;
  http.begin(buf);
  int responseCode = http.GET();
  String payload = http.getString();
  Serial.printf("Response code %d: request=%s\n",responseCode,(char*)payload.c_str());
  http.end();
  if (responseCode == 200)
  {
    int idx = payload.indexOf("<p>At ");
    if (idx > 0)
    {
      // 01234567890123456789012345
      // <p>At 2024/02/24,11:22:33
      timeFromIotHub = payload.substring(idx+6,idx+25);
    }
  }
  return responseCode == 200; // true is success
}

char buf[384];
char buf1[384];
int lastSec = -1;
//--------------------------------------------------
// Operational tasks
void loop() 
{
  LEDOFF;
  for (;;)
  {
    int currentSec = rtc.getSecond();
    if (currentSec != lastSec)
    {
      //--- one second tasks
      lastSec = currentSec;
      ledmode ^= 1;
      digitalWrite(SIGNALLED,ledmode);
      // The ESP32 has a watch-dog timer (WDT).  This is a timer that counts from a starting
      // point down to 0.   By calling esp_task_wdt_reset() you can reset the WDT back to it's
      // starting point.  This is "kicking the WDT". 
      //
      // If for some reason, the WDT doesn't get reset before it gets down to 0, then
      // the ESP32 will be reset and it will reboot.
      //
      // This is a "last ditch" effort to recover the application in case it should run
      // into some unexpected dead state such that the WDT isn't getting kicked in time.
      esp_task_wdt_reset(); // reset watch-dog timer
    }
        
    // keep track of minutes going by in a variable minuteCounter  
    int currentMin = rtc.getMinute();
    if (currentMin != lastMin)
    {
      //--- one minute tasks
      LEDON;
      lastMin = currentMin;
      minuteCounter++;
      Serial.print("Minute processing: ");
      Serial.println(minuteCounter);

      if ((minuteCounter % 60) == 59) timeForScan = true; // rescan BLE each hour
      
      // now read all devices that are due for this minute
      for (int i = 0; i < nValues; i++)
      {
        ValueToRead* p = vals2read[i];
        long mod = minuteCounter % p->minutesBetweenReads;
        if ((mod == 0) && (p->deviceAddr[0] != '\0'))
        {
          // it's the right minute to read the value,
          // and we have seen the device advertise in a BLE scan

          readValue(pClient, p, buf,256);
          String ttag = rtc.getTime("%Y/%m/%d,%H:%M:%S");
          sprintf(buf,"%s,%s,%s",(char*)ttag.c_str(), p->valueTag,buf);
          int sts = measuredData.push(buf);
          if (sts == 0) Serial.println("Measurement queue overflow");
        }
      }
      LEDOFF;
    }

    //-- on demand task - rescan for BLE devices
    if (timeForScan)
    {
      BLEScanResults foundDevices = pBLEScan->start(scanTime, false);
      Serial.print("Devices found: ");
      Serial.println(foundDevices.getCount());
      Serial.println("Scan done!");
      pBLEScan->clearResults();   
    
      timeForScan = false;
    }

    //--- on demand task - if WiFi disconnects, try to reconnect
    if (WiFi.status() != WL_CONNECTED)
    {
      // WiFi is disconnected, so try a reconnect
      delay(200);
      connectToWiFi();
      if (wifiIsConnected) getNtpTime();
      lastMin = rtc.getMinute();
    }

    // push any queued data to the IoT hub
    if (measuredData.isEmpty() == 0)
    {
      // send any queued measurement data to server
      buf1[0] = '\0';
      int sts = measuredData.pop(buf1,384);
      if (sts && (strlen(buf1)>0))
      {
        sts = forwardValueToIotHub(buf1);
        if (!sts) 
        {
          measuredData.push(buf1); // didn't go, push it back to try later
          Serial.println("Send to IoT hub failed");
        }
      }
    }

    // set RTC
    if (timeFromIotHub != "")
    {
      Serial.print("Set time: "); Serial.println(timeFromIotHub);
      String ttag = rtc.getTime("%Y/%m/%d,%H:%M:%S");
      Serial.println(ttag);
      timeFromIotHub = "";
    }
    
  }

}
