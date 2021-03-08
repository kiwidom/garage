/*
 * Copyright (c) Mnemonics Ltd
 *
 * broadleaf_garage.ino
 *
 * This is a specific ESP-01 file to load onto an attached ESP8622 for controlling a garage door
 * ESP8622 connected to a https://www.jaycar.co.nz/duinotech-smart-wifi-relay-kit/p/XC3802
 * Open position switch   = D6
 * Closed position switch = D7
 * Relay is connected to D8 - this has an internal pull-up so that on BOOT there is no relay clicking 
 *
 * ----------------------------------------------------------------------------------------------------
 * Uses HTML webpage to call via it's IP address examples below
 * 
 * 192.168.1.69 - Home page with buttons for PULSE door output, OPEN and CLOSE which will only be allowed
 *                if the door switches are in the opposing position as above
 *
 * 192.168.1.69/logs - Show the last 20 log outputs 
 * 192.168.1.69/pulse - 1 second pulse on the garage door open switch
 * 192.168.1.69/close - 1 second pulse on the garage door open switch (if the two input switches reflect an OPEN state)
 * 192.168.1.69/open  - 1 second pulse on the garage door open switch (if the two input switches reflect an CLOSED state)
 *
 * ----------------------------------------------------------------------------------------------------
 *
 * This program listens to for a connected client with a GET request for either logs/pulse/open/close
 * This program will send updates to mnenomics.co.nz/garage/rxState?state={OPEN|MOVING|CLOSED|BOOTING}&sender={xx.xx.xx.xx - [device name]
 *
 *
 *
 * @date  Feb 2021  
 *
 *
 */
 String __version__ = "$Revision: 2.1 $";
// Load Wi-Fi and NTP and UDP libraries
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP_EEPROM.h>
#include "broadleaf_garage_1.h"

const long utcOffsetInSeconds = 43200;
// New Zealand UTC = 12 : 12 * 60 * 60 = 43200

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "asia.pool.ntp.org", utcOffsetInSeconds);

char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};


// Replace with your network credentials
//const char* ssid     = "SPARK-GLDE53";
//const char* password = "X3RTR4ZFJ9";
//String remote_WEBADDRESS = "mnemonics.co.nz";
//String remote_PAGEDEST = "/garage/rxState.php";

//Client sent strings
const String _local_SSID = "local_SSID=";
const String _local_PASS = "local_PASS=";
const String _remote_WEBADDRESS = "remote_WEBADDRESS=";
const String _remote_PAGEDEST = "remote_PAGEDEST=";

const String reqFactoryReset = "/FACTORY/";
const String reqReboot = "/REBOOT/";
const String reqWifiRestart = "/WifiRestart/";
const String reqSave = "/save/";
const String reqLoad = "/load/";
const String reqInfo = "/info/";
const String reqPulse = "/pulse/";
const String reqOpen = "/open/";
const String reqClose = "/close/";
const String reqLogs = "/logs/";
const String reqTest = "/test/";

// Input Swich ports
const int closedSwitch = 13; 	//13 = D7
const int openSwitch = 12; 		//12 = D6
const int spare1 = 14; 			//14 = D5
const int spare2 = 16; 			//16 = D0
const int relaySwitch = 15; 	//15 = D8 - internal pull-up (no clicking on boot!!)


// Current time
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0; 
// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 2000;

unsigned long previousChangeTime = 0;

// PAGES
#define ERROR		0
#define LANDING 	1
#define DETAILS	 	2
#define LOGS	 	3

byte pageRequest = ERROR;

#define LOG			0
#define SERIAL		1
#define BOTH 		2


/*****************************************
 * DEBUG stuff
 *
 *****************************************/
// for outputing extra info to the Serial port
#define DEBUG_NONE 		0
#define DEBUG_LEVEL_1 	1
#define DEBUG_LEVEL_2	2
#define DEBUG_LEVEL_3	3
#define DEBUG_DATA_LOG	4

int DEBUG = DEBUG_NONE;

// MACROS
#define True 1
#define False 0

// Set web server port number to 80
WiFiServer server(80);

// Variable to store the HTTP request
int doorClosed = 0;
int doorOpen = 0;

int lastDoorClosed; 
int lastDoorOpen; 

String myIP = "";
String myName = "";

#define LOGSIZE 50

struct _logs {
  char output[50];
  char timeStamp[30];
}logArray[LOGSIZE];

struct _logs *currentLogPtr = logArray;
int currentLog = 0;

int failedToSend = 0;

// Storing AP's seen
struct _apList {
	char ssid[30];
	char pass[30];
}apArray[20];

struct _apList *currentApPtr = apArray;
int numOfAPs = 0;


struct _config{
	byte done;
	char local_SSID[50];
	char local_PASS[50];
	char remote_WEBADDRESS[50];
	char remote_PAGEDEST[50];
}storage[1];


// get a pointer to the storage structure
struct _config *storagePtr = storage;
byte MAGIC = 123;

int wifiAttempts = 0;

/**
 * Name: setup
 * 
 * Return: None
 * 
 * Parameters: None
 * 
 * Description: Mandatory setup function - on entry/restart/boot
 *
 */
void setup()
{

	initHardware();
	startWifi();

	// Call the Wifi routines
	while (WiFi.status() != WL_CONNECTED) 
	{
		Serial.print(".");
		startWifi();
		delay(1000);
	}
		
		
	delay(2000);
	// start the time client
	timeClient.begin();
	//Let Mnemonics we're on board
	updateWeb( storagePtr[0].remote_PAGEDEST, "BOOTING" );
	//Store the initial state of the doors
	lastDoorClosed = digitalRead(closedSwitch);
	lastDoorOpen = digitalRead(openSwitch);
	//add successful booting to the logs
	String bootString = "Successful Booting ";
	bootString += __version__;
	signalEvent(bootString, BOTH);
}


/**
 * Name: startWifi
 * 
 * Return: None
 * 
 * Parameters: None
 * 
 * Description: Starts up Wifi and tries to connect to home router
 *
 */
void startWifi()
{
	wifiAttempts++;
	signalEvent("Starting WiFi...", SERIAL);
	int wait = 6000;
	long int time = millis();
	
	WiFi.mode(WIFI_OFF);
	delay(10);
	
	// ensure we are only a station
	WiFi.mode(WIFI_STA);
	
	// Set the wifi client name
	WiFi.hostname("broadleaf_Garage");

	// Start the wifi and connect to the Router
	WiFi.begin(storagePtr[0].local_SSID, storagePtr[0].local_PASS);
	// Wait until we are connected...
	while (WiFi.status() != WL_CONNECTED) 
	{
		delay(500);
		// but only try for 6 seconds     
		if ( millis() > (time + wait) )
		  break;
	}
	// if we didn't connect - log it amd drop out - we'll get another chance when we try to log to Mnemonics and wifi isn't up
	if ( WiFi.status() != WL_CONNECTED )
	{
		signalEvent("FAILED to get WiFi Connection - ABORTING!", BOTH);
		return;
	}

	// store the variables
	myIP = WiFi.localIP().toString();
	myName = WiFi.hostname();  

	// log it
	signalEvent("Connected to Router", BOTH);
	signalEvent(myIP, SERIAL);

	// Now start the Server - for connecting to mnemonics
	server.begin();
	
	return;
}
/**
 * Name: initHardware
 *
 * Return: nothing
 *
 * Parameters: nothing 
 *
 * Description: Start the Serial port, delay 3 secs and signal to the Controller that we are up and running
 */
void initHardware()
{
	int i;
	
	Serial.begin(115200);
	
	delay(300);
	signalEvent("ESP Starting!", SERIAL);
	
	loadFromEEPROM();
	
	//presetEEPROM();
	
	// Lets get a list of Available AP's
	numOfAPs = WiFi.scanNetworks();
	
	for ( i = 0; i <= numOfAPs; i++ )
	{
		WiFi.SSID(i).toCharArray(currentApPtr[i].ssid, 30); 	
	}
	
	// Start hardware states
	pinMode(relaySwitch, OUTPUT);
	digitalWrite(relaySwitch, LOW);
	pinMode(closedSwitch, INPUT_PULLUP);
	pinMode(openSwitch, INPUT_PULLUP); 
	
	Serial.println("local_SSID=xxx& to change SSID to connect to");
	Serial.println("local_PASS=yyy& to change password of connectio");
	Serial.println("remote_WEBADDRESS=zzz.xxx.yyy& for logging");
	Serial.println("remote_PAGEDEST=/dest/page.php& actual page on above site");
	Serial.println("/FACTORY/ to return to factory defaults");   
	Serial.println("other commands: /ver/  | /info/ | /WifiRestart/ | /REBOOT/ | /load/ | /save/ | /logs/");   

	Serial.println("[INFO-START]");
	Serial.println(storagePtr[0].done);
	Serial.println(storagePtr[0].local_SSID);
	Serial.println(storagePtr[0].local_PASS);
	Serial.println(storagePtr[0].remote_WEBADDRESS);
	Serial.println(storagePtr[0].remote_PAGEDEST);
	Serial.println("[INFO-END]");

}



/**
 * Name: loop
 * 
 * Return: None
 * 
 * Parameters: None
 * 
 * Description: Mandatory loop function
 *
 */
void loop()
{
  // check if anyone has connected to us and is requesting a page
  checkForClient();
  // check if the input switches have changed - need to know if the door has moved manually
  checkSystemStatus();
  //check health of things
  checkHealth();
  //Check Serial port - in case we've connected via Serial port (IDE)
  checkSerialData();
  
}

/**
 * Name: checkSerialData
 * 
 * Return: 1 if we found anything, 0 if not (tells caller if there was anything processable
 * 
 * Parameters: None
 * 
 * Description: See if the Arduino has sent us updated display, or new page structure info
 *
 */
byte checkSerialData()
{
		
	// If the controller has sent something
	if ( Serial.available() > 0 )
	{
		String data = Serial.readString();
		
		int status = WiFi.status();

		int i = 0;
		String tempD = "";
		int pingNum = 0;
		
		int understood = false;
	
		
		// Have we been requested to send our file revision
		if (data.indexOf("/ver/") != -1)
		{
			Serial.println("/Version=" + String(__version__) + "/");
			understood = true;
		}
		// Loop until watchdog kicks us 
		if ( data.indexOf(reqReboot) != -1 )
		{
			ESP.wdtDisable();
 			while (1){};
		}
		if (data.indexOf(reqWifiRestart) != -1)
		{
			startWifi();
			understood = true;
		}
		if (data.indexOf(reqLogs) != -1)
		{
			understood = true;
			for ( int i = 0; i < LOGSIZE; i++ )
			{
				Serial.print("Output: ");
				Serial.print(currentLogPtr[currentLog].output);
				Serial.print(" - ");
				Serial.println(currentLogPtr[currentLog].timeStamp);
		

				currentLog--;
				// check if we need to wrap round
				if ( currentLog <= -1 )
				  currentLog = LOGSIZE-1 ;
			}
		}
		if (data.indexOf(reqInfo) != -1)
		{
			Serial.println(storagePtr[0].done);
			Serial.println(storagePtr[0].local_SSID);
			Serial.println(storagePtr[0].local_PASS);
			Serial.println(storagePtr[0].remote_WEBADDRESS);
			Serial.println(storagePtr[0].remote_PAGEDEST);
			Serial.println(myIP);
			Serial.print("Local network Status [");
			Serial.print( status );
			Serial.println("]");
	
	 		// print the received signal strength:
  			long rssi = WiFi.RSSI();
  			Serial.print("RSSI:");
  			Serial.println(rssi);
			understood = true;
		}
		if (data.indexOf(reqLoad) != -1)
		{
			loadFromEEPROM();
			understood = true;
		}
		if (data.indexOf(reqSave) != -1)
		{
			saveToEEPROM();
			understood = true;
		}
		// Have we been requested to send our file revision
		if (data.indexOf("/debug/") != -1)
		{
			DEBUG++;
			DEBUG = ( DEBUG > DEBUG_LEVEL_3 ) ? DEBUG_NONE : DEBUG;
			Serial.println("DEBUG_LEVEL_" + String(DEBUG));
			understood = true;
		}
		if (data.indexOf(reqFactoryReset) != -1)
		{
			understood = true;
			presetEEPROM();
		}
		
		if ( data.indexOf(_local_SSID) != -1 )
		{
			understood = true;
			String rxlocalSSID = data.substring(data.indexOf(_local_SSID)+_local_SSID.length(), data.indexOf('&', data.indexOf(_local_SSID)+_local_SSID.length() ));
			rxlocalSSID.toCharArray(storagePtr[0].local_SSID, rxlocalSSID.length()+1);
			//saveToEEPROM();
		}
		if ( data.indexOf(_local_PASS) != -1 )
		{
			understood = true;
			String rxlocalPASS = data.substring(data.indexOf(_local_PASS)+_local_PASS.length(), data.indexOf('&', data.indexOf(_local_PASS)+_local_PASS.length() ));
			rxlocalPASS.toCharArray(storagePtr[0].local_PASS, rxlocalPASS.length()+1);
			//saveToEEPROM();

		}
		if ( data.indexOf(_remote_WEBADDRESS) != -1 )
		{
			understood = true;
			String rxRemWEB = data.substring(data.indexOf(_remote_WEBADDRESS)+_remote_WEBADDRESS.length(), data.indexOf('&', data.indexOf(_remote_WEBADDRESS)+_remote_WEBADDRESS.length() ));
			rxRemWEB.toCharArray(storagePtr[0].remote_WEBADDRESS, rxRemWEB.length()+1);
			//saveToEEPROM();

		}
		if ( data.indexOf(_remote_PAGEDEST) != -1 )
		{
			understood = true;
			String rxRemWEBDEST = data.substring(data.indexOf(_remote_PAGEDEST)+_remote_PAGEDEST.length(), data.indexOf('&', data.indexOf(_remote_PAGEDEST)+_remote_PAGEDEST.length() ));
			/*rxRemWEBDEST.replace("%2F", "/");
			rxRemWEBDEST.replace("%3F", "?");
			rxRemWEBDEST.replace("%26", "&");
			rxRemWEBDEST.replace("%3D", "=");*/
			rxRemWEBDEST.toCharArray(storagePtr[0].remote_PAGEDEST, rxRemWEBDEST.length()+1);
			saveToEEPROM();

		}
		
		if ( understood == false )
		{
			Serial.println("Unknown Command");		
		}	
		return 1;
	}
	return 0;
}

/**
 * Name: checkHealth
 * 
 * Return: None
 * 
 * Parameters: None
 * 
 * Description: check flags/counters and remedy if necessary
 *
 */
void checkHealth()
{
	// first check fails to send
	if ( failedToSend >= 2 )
	{
		signalEvent("Too many Failed to Sends...restarting Wifi", BOTH);
		WiFi.disconnect();
		delay(100);
		startWifi();
		//failedToSend = 0; //let a successful send reset the counter
	}
	


}


/**
 * Name: checkSystemStatus
 * 
 * Return: None
 * 
 * Parameters: None
 * 
 * Description: check if a switch has changed since last time or if Wifi has gone
 *
 */
void checkSystemStatus()
{
	unsigned long currentChangeTime = millis();
	// Waiting until 5 seconds has passed before checking
	if (currentChangeTime < (previousChangeTime + 5000) )
		return;

	previousChangeTime = currentChangeTime;

	int currentDoorClosed = digitalRead(closedSwitch);
	int currentDoorOpen = digitalRead(openSwitch); 

    // only update webserver if there has been a state change
    if ( currentDoorClosed != lastDoorClosed || currentDoorOpen != lastDoorOpen )
    {
      String doorState = getState();
      updateWeb( storagePtr[0].remote_PAGEDEST, doorState );
      signalEvent("Change of Door State Seen", BOTH);
    }
    
    // Also check if we are attached to the local router
    if ( WiFi.status() == WL_CONNECTION_LOST )
	{
		signalEvent("LOST WiFi Connection - Restarting Wifi!", BOTH);
		startWifi();
	}
    
      
}


/**
 * Name: getTimeStamp
 * 
 * Return: String ...e.g. Tuesday 04:12:13
 * 
 * Parameters: None
 * 
 * Description: Uses NTP server to get a timestamp 
 *
 */
String getTimeStamp()
{
	timeClient.update();

	String timeStamp = daysOfTheWeek[timeClient.getDay()];
	timeStamp += " ";
	timeStamp += timeClient.getFormattedTime();

	return timeStamp;
  
}


/**
 * Name: signalEvent
 * 
 * Return: Nothing
 * 
 * Parameters: String to add to log Struct
 * 
 * Description: adds a log entry, and time stamp to the log struct - overwritting in a loop
 *
 */
void signalEvent(String output, byte where)
{
	getTimeStamp().toCharArray(currentLogPtr[currentLog].timeStamp, 30);
	
	if ( where == BOTH || where == LOG )
	{
		if ( currentLog >= LOGSIZE-1 )
			currentLog = 0;
		else 
			currentLog++;

		output.toCharArray(currentLogPtr[currentLog].output, 50);
	}
	if ( where == BOTH || where == SERIAL )
	{
		Serial.println(output);	
	}
  
}


/**
 * Name: getState
 * 
 * Return: String - state based on logic
 * 
 * Parameters: None
 * 
 * Description: returns the state as a string based on the switch states
 *
 */
String getState()
{
	lastDoorClosed = digitalRead(closedSwitch);
	lastDoorOpen = digitalRead(openSwitch);

	if ( lastDoorClosed == LOW  && lastDoorOpen == HIGH ) 
		return "CLOSED";
	if ( lastDoorClosed == HIGH  && lastDoorOpen == LOW ) 
		return "OPEN";
	if ( lastDoorClosed == HIGH  && lastDoorOpen == HIGH ) 
		return "MOVING";
	else
		return "ERROR";

}


/**
 * Name: chckForRequest
 * 
 * Return: None
 * 
 * Parameters: None
 * 
 * Description: check for attaching clients requesting something
 *
 */
void checkForClient()
{
	
	String s="";
	pageRequest = LANDING;
	
	// establish current position for decision logic below
	doorClosed = digitalRead(closedSwitch);
	doorOpen = digitalRead(openSwitch);

	// Check if a client has connected
	WiFiClient client = server.available();
	if ( !client ) 
	{
		return;
	}
	
	// Read the first line of the request
	String req = client.readStringUntil('\r');
	
	//req.replace("GET ", "");
	//req.replace(" HTTP/1.1", "");
	
	if ( req.indexOf(reqReboot) != -1 )
	{
		Serial.println("REBOOT REQUESTED");
		ESP.wdtDisable();
 		while (1){};
	}
	if ( req.indexOf(reqWifiRestart) != -1 )
	{
		Serial.println("Wifi Restart Requested");
		startWifi();
		pageRequest = LANDING;
	}
	if ( req.indexOf(reqFactoryReset) != -1 )
	{
		presetEEPROM();
		pageRequest = DETAILS;
	}
	if ( req.indexOf(reqInfo) != -1 )
	{
		pageRequest = DETAILS;
	}
	if ( req.indexOf(reqPulse) != -1 )
	{
		activate();
		updateWeb( storagePtr[0].remote_PAGEDEST, "Client - PULSE" );
		signalEvent("Client request for PULSE", LOG);
		pageRequest = LANDING;
	}
	if ( req.indexOf(reqOpen) != -1 && doorClosed == LOW )
	{
		activate();
		updateWeb( storagePtr[0].remote_PAGEDEST, "Client - OPEN" );
		signalEvent("Client request for OPEN", LOG);
		pageRequest = LANDING;
	}
	if ( req.indexOf(reqClose) != -1 && doorOpen == LOW  )
	{
		activate();
		updateWeb( storagePtr[0].remote_PAGEDEST, "Cient - CLOSE" );
		signalEvent("Client request for CLOSE", LOG);
		pageRequest = LANDING;
	}
	if ( req.indexOf(reqTest) != -1 )
	{
		updateWeb( storagePtr[0].remote_PAGEDEST, "TESTING" );
		signalEvent("Testing Mnemonics Connection", BOTH);
		pageRequest = DETAILS;
	}
	if ( req.indexOf(reqLogs) != -1 )
	{
		pageRequest = LOGS;
	}
	
	client.flush();
	
	switch(pageRequest)
	{
		case ERROR: s = showPage_error(); break;
		case LANDING: s = showPage_landing(); break;
		case DETAILS: s = showPage_details(); break;
		case LOGS: s = showPage_logs(); break;
		default: s = showPage_landing(); break;
	}
	
	client.print(s);
	
	delay(1);
	
	client.flush();
	
}

/**
 * Name: showPage_error
 *
 * Return: String of HTML for the showPage_error
 *
 * Parameters: nothing 
 *
 * Description: Determines what to do
 */
String showPage_error()
{
	String webpage = htmlStart;
	webpage += header;	
	webpage += "<H1>ERROR - Unknown request</H1>";
    
	webpage += end;  

	return webpage;
}



/**
 * Name: showPage_landing
 *
 * Return: String of HTML for the showPage_landing
 *
 * Parameters: nothing 
 *
 * Description: Determines what to do
 */
String showPage_landing()
{
	// establish current position for decision logic below
	doorClosed = digitalRead(closedSwitch);
	doorOpen = digitalRead(openSwitch);
	
	String state = getState();
	
	String webpage = htmlStart;
	webpage += header;	
	webpage += createJavascript();	
	webpage += "<a href=\"http://";
    webpage +=  myIP;
    webpage += "/\"><h1>BROADLEAF GARAGE SERVER</a></h1>";
 	
 	webpage += createMenu();
 	
 	// Check switches for current state
	if ( doorClosed == LOW ) { webpage += "<h3>Currently CLOSED</h3>"; }
	else if ( doorOpen == LOW ) { webpage += "<h3>Currently OPEN</h3>"; }
	else { webpage += "<h3>Currently MOVING</h3>"; }

	// Report on Switch 1
	if ( doorClosed == HIGH ) { webpage += "<p>Switch1 [CLOSED SWITCH] = OPEN<p>"; }
	else { webpage += "<p>Switch1 [CLOSED SWITCH] = CLOSED<p>"; }
	// Report on switch 2
	if ( doorOpen == HIGH ) { webpage += "<p>Switch2 [OPEN SWITCH] = OPEN<p>"; }
	else { webpage += "<p>Switch2 [OPEN SWITCH] = CLOSED<p>"; }
    
    webpage += "<p><a href=\"/pulse/\"><button class=\"button\">PULSE</button></a></p>";
    
    if ( state.indexOf("OPEN") != -1  || state.indexOf("MOVING") != -1 )
    {
    	webpage += "<p><button class=\"button3\">OPEN</button></p>";	
    }
    else 
    	webpage += "<p><a href=\"/open/\"><button class=\"button\">OPEN</button></a></p>";
    	
    if ( state.indexOf("CLOSED") != -1 || state.indexOf("MOVING") != -1 )
    {
    	webpage += "<p><button class=\"button3\">CLOSED</button></p>";	
    }
    else 
    	webpage += "<p><a href=\"/close/\"><button class=\"button\">CLOSE</button></a></p>";
    	
    webpage += "<p><a href=\"/test/\"><button class=\"button\">TEST COMMS</button></a></p>";
    
	webpage += end;  

	return webpage;
				
}
/**
 * Name: showPage_details
 *
 * Return: String of HTML for the showPage_details
 *
 * Parameters: nothing 
 *
 * Description: Determines what to do
 */
String showPage_details()
{
	long rssi = WiFi.RSSI();
	// establish current position for decision logic below
	doorClosed = digitalRead(closedSwitch);
	doorOpen = digitalRead(openSwitch);

	String webpage = htmlStart;
	webpage += header;	
	webpage += createJavascript();	
	webpage += "<a href=\"http://";
    webpage +=  myIP;
    webpage += "/\"><h1>BROADLEAF GARAGE SERVER</a></h1>";
    
    webpage += createMenu();
   
   // Check switches for current state
	if ( doorClosed == LOW ) { webpage += "<h3>Currently CLOSED</h3>"; }
	else if ( doorOpen == LOW ) { webpage += "<h3>Currently OPEN</h3>"; }
	else { webpage += "<h3>Currently MOVING</h3>"; }

	// Report on Switch 1
	if ( doorClosed == HIGH ) { webpage += "<p>Switch1 [CLOSED SWITCH] = OPEN<p>"; }
	else { webpage += "<p>Switch1 [CLOSED SWITCH] = CLOSED<p>"; }
	// Report on switch 2
	if ( doorOpen == HIGH ) { webpage += "<p>Switch2 [OPEN SWITCH] = OPEN<p>"; }
	else { webpage += "<p>Switch2 [OPEN SWITCH] = CLOSED<p>"; }
    
    webpage += "<p>ROUTER SSID: ";
    webpage += storagePtr[0].local_SSID;
    webpage += "</p>";
    webpage += "<p>Remote Wesite Address: ";
    webpage += storagePtr[0].remote_WEBADDRESS;
    webpage += "</p>";
    webpage += "<p>Page Destination: ";
    webpage += storagePtr[0].remote_PAGEDEST;
    webpage += "</p>";
    webpage += "<p>";
    webpage += "Wifi Status: <br>WL_IDLE_STATUS:0 <br> WL_NO_SSID_AVAIL:1 <br> WL_SCAN_COMPLETED:2 <br> WL_CONNECTED:3 <br> WL_CONNECT_FAILED:4 <br> WL_CONNECTION_LOST:5 <br> WL_DISCONNECTED:6 <br> STATUS = ";
    webpage += WiFi.status();
    webpage += "</p>";
    webpage += "<p>";
    webpage += "RSSI: ";
    webpage += rssi;
    webpage += "</p>";
     webpage += "<p>";
    webpage += "Attempts to connect: ";
    webpage += wifiAttempts;
    webpage += "</p>";
    webpage += "<p>";
    webpage += __version__;
    webpage += "</p>";
    
    webpage += "<p>Last Output: ";
	webpage += currentLogPtr[currentLog].output;
	webpage += " - ";
	webpage += currentLogPtr[currentLog].timeStamp;
	webpage += "<p>";
    
	webpage += end;  

	return webpage;
}
/**
 * Name: showPage_logs
 *
 * Return: String of HTML for the showPage_logs
 *
 * Parameters: nothing 
 *
 * Description: Determines what to do
 */
String showPage_logs()
{
	Serial.println("Building Log page");
	
	String webpage = htmlStart;
	webpage += header;
	webpage += createJavascript();	
	webpage += "<a href=\"http://";
    webpage +=  myIP;
    webpage += "/\"><h1>BROADLEAF GARAGE SERVER</a></h1>";
    
    webpage += createMenu();
    
    int logCount = currentLog;
    
    for ( int i = 0; i < LOGSIZE; i++ )
	{
		webpage += "<p>Output: ";
		webpage += currentLogPtr[logCount].output;
		webpage += " - ";
		webpage += currentLogPtr[logCount].timeStamp;
		webpage += "<p>";

		logCount--;
		// check if we need to wrap round
		if ( logCount <= -1 )
		  logCount = LOGSIZE-1 ;
	}
	
	webpage += end;
	
	return webpage;
}

/**
 * Name: createMenu
 *
 * Return: String of HTML
 *
 * Parameters: nothing 
 *
 * Description: Creates a string of HTML for the menubar for a specific page
 */
String createMenu()
{
	
	String menu = "<h4>";
	menu +="<a href=\"http://";
    menu +=  myIP;
    menu += "/logs/\"><button class=\"button2\">Logs</button></a>";
	menu += "<a href=\"http://";
    menu +=  myIP;
    menu += "/info/\"><button class=\"button2\">Details</button></a>";
	menu += "<a href=\"http://";
    menu +=  myIP;
    menu += "\"><button class=\"button2\">Home</button></a>";
    menu += "<a href=\"http://";
    menu +=  myIP;
    menu += "/WifiRestart/\"><button class=\"button2\">Restart Wifi</button></a>";
    menu += "<a href=\"http://";
    menu +=  myIP;
    menu += "/REBOOT/\"><button class=\"button2\">Reboot</button></a>";
	menu += "</h4>";

	return menu;
}

/**
 * Name: createJavascript
 *
 * Return: String of HTML
 *
 * Parameters: nothing 
 *
 * Description: Creates a string of HTML for the menubar for a specific page
 */
String createJavascript()
{
	return "";
	
	String javascript;
	javascript = "<script> setTimeout(function(){ window.location.href = 'http://";
    javascript += myIP;
    javascript += "/';  }, 10000); </script>";
      
	return javascript;
}


/**
 * Name: activtate
 * 
 * Return: None
 * 
 * Parameters: None
 * 
 * Description: Pulses the garage door via relay on attached board
 *
 */
void activate()
{
	digitalWrite(relaySwitch, HIGH);
	delay(1000);
	digitalWrite(relaySwitch, LOW);
}


/**
 * Name: updateWeb
 *
 * Return: nothing
 *
 * Parameters: nothing 
 *
 * Description: Send data to a website... Given dir/page then ?data={data string} - then appends the &sender=(my_SSID)
 */
void updateWeb(String pageDestination, String dataToSend)
{
	// Check if we are not connected - if not then restart everything
	if ( WiFi.status() != WL_CONNECTED )
	{
		signalEvent("Wifi Gone - Restarting Wifi", BOTH);
		WiFi.disconnect();
		delay(100);
		startWifi();
	}
	
	
	// Buld up string to send to mnemonics
	String GET = pageDestination + "?state=" + dataToSend + "&sender=" + myIP + "-[" + myName + "]";

	// Create the Client 
	WiFiClient sender;
	// set the host port
	const uint16_t port = 80;
	// use the right address
	String host = storagePtr[0].remote_WEBADDRESS;

	if (!sender.connect(host, port)) 
	{
		// If we don't have a connection to mnemonics - log the failure and drop out
		signalEvent("FAILED to connect to Mnemonics", BOTH);
		failedToSend++;
		return;
	}
	// if we have got here - then reset the faileds....
	failedToSend = 0;

	// This will send the request to the server
	sender.print("GET " + GET);
	sender.print(" HTTP/1.1\r\nHost: ");
	sender.print(host);
	sender.print("\r\nConnection: close\r\n");
	sender.println();


	// wait a moment
	delay(1000);

	// Read back the first line of the response - Ideally: HTTP/1.1 200 OK
	String line = sender.readStringUntil('\r');
	signalEvent(line, LOG);

	sender.stop();
}


/**
 * Name: saveToEEPROM
 *
 * Return: nothing
 *
 * Parameters: nothing 
 *
 * Description: Saves current data on settings to EEPROM
 */
void saveToEEPROM()
{

	EEPROM.begin(sizeof(storage));
	EEPROM.put(0,storage);
	
	// write the data to EEPROM
  	boolean ok = EEPROM.commit();
  	
  	if ( ok ) 
  	{
  		Serial.println("SAVED: [");
		Serial.println(storagePtr[0].done);
		Serial.println(storagePtr[0].local_SSID);
		Serial.println(storagePtr[0].local_PASS);
		Serial.println(storagePtr[0].remote_WEBADDRESS);
		Serial.println(storagePtr[0].remote_PAGEDEST);
		Serial.println("]");
  	}
  	else
  	{
  		Serial.println("Failed to Save to EEPROM");	
  	}
	
	EEPROM.end();
	
}
/**
 * Name: loadFromEEPROM
 *
 * Return: nothing
 *
 * Parameters: nothing 
 *
 * Description: Loads current data on settings from EEPROM
 */
void loadFromEEPROM()
{
	EEPROM.begin(sizeof(storage));
	
	byte check;
	EEPROM.get(0, check);
	
	if ( check != MAGIC )
	{
		Serial.print("MAGIC CHECK FAILED, saw=");
		Serial.println(check);
		presetEEPROM();
		return;
	}
	
	EEPROM.get(0, storage);
	EEPROM.end();

	if ( DEBUG >= DEBUG_NONE )
	{
		Serial.println("EEPROM...[");
		Serial.println(storagePtr[0].done);
		Serial.println(storagePtr[0].local_SSID);
		Serial.println(storagePtr[0].local_PASS);
		Serial.println(storagePtr[0].remote_WEBADDRESS);
		Serial.println(storagePtr[0].remote_PAGEDEST);
		Serial.println("]");
	}
	

}
/**
 * Name: presetEEPROM
 *
 * Return: nothing
 *
 * Parameters: nothing 
 *
 * Description: Presets the Data for the EEPROM on my things!
 */
void presetEEPROM()
{
	Serial.println("[PresetEEPROM]");

	String local_SSID = "SPARK-GLDE53";
	String local_PASS = "X3RTR4ZFJ9";
	String remote_WEBADDRESS = "www.mnemonics.co.nz";
	String remote_PAGEDEST = "/garage/rxState.php";
	
	storagePtr[0].done = 123;
	local_SSID.toCharArray(storagePtr[0].local_SSID, 50);
	local_PASS.toCharArray(storagePtr[0].local_PASS, 50);
	remote_WEBADDRESS.toCharArray(storagePtr[0].remote_WEBADDRESS, 50);
	remote_PAGEDEST.toCharArray(storagePtr[0].remote_PAGEDEST, 50);
	
	//Serial.print("Size of Storage array = ");
	//Serial.println(sizeof(storage));
	
	EEPROM.begin(sizeof(storage));
	
	EEPROM.put(0, storage);
	
	boolean ok = EEPROM.commitReset();
  	Serial.println((ok) ? "FACTORY DEFAULTS RESET OK" : "Commit failed - EEPROM unchanged");

	EEPROM.end();
	
	/*Serial.println(storagePtr[0].local_SSID);
	Serial.println(storagePtr[0].local_PASS);
	Serial.println(storagePtr[0].remote_WEBADDRESS);
	Serial.println(storagePtr[0].remote_PAGEDEST);*/

}




