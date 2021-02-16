/*
 * Copyright (c) Mnemonics Ltd
 *
 * broadleaf_garage.ino
 *
 * This is a specific ESP-01 file to load onto an attached ESP-01 for controlling a garage door
 * ESP-01 connected to a https://www.jaycar.co.nz/duinotech-smart-wifi-relay-kit/p/XC3804
 * Using two soldered lines to Pin2 and Pin0 of ESP-01 Pin2 - Closed door position Reed switch Pin0 - Open door position Reed Switch
 *
 * ----------------------------------------------------------------------------------------------------
 * Uses HTML webpage to call via it's IP address examples below
 * 
 * 192.168.1.75 - Home page with buttons for PULSE door output, OPEN and CLOSE which will only be allowed
 *                if the door switches are in the opposing position as above
 *
 * 192.168.1.75/logs - Show the last 20 log outputs 
 * 192.168.1.75/pulse - 1 second pulse on the garage door open switch
 * 192.168.1.75/close - 1 second pulse on the garage door open switch (if the two input switches reflect an OPEN state)
 * 192.168.1.75/open  - 1 second pulse on the garage door open switch (if the two input switches reflect an CLOSED state)
 *
 * ----------------------------------------------------------------------------------------------------
 *
 * This program listens to for a connected client with a GET request for either logs/pulse/open/close
 * This program will send updates to mnenomics.co.nz/garage/rxState?state={OPEN|MOVING|CLOSED|BOOTING}&sender={xx.xx.xx.xx - [device name]
 *
 *
 * IMPORTANT: TURN THE ESP DEVICE ON WHEN THE DOOR IS MOVING - If either of the Pin0 or Pin2 switches are on - it will not boot into run mode
 *
 * @date  Feb 2021  
 *
 *
 */
 String __version__ = "$Revision: 3 $";
// Load Wi-Fi and NTP and UDP libraries
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

const long utcOffsetInSeconds = 43200;
// New Zealand UTC = 12 : 12 * 60 * 60 = 43200

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "asia.pool.ntp.org", utcOffsetInSeconds);

char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};


// Replace with your network credentials
const char* ssid     = "SPARK-GLDE53";
const char* password = "X3RTR4ZFJ9";

String remote_WEBADDRESS = "mnemonics.co.nz";
String remote_PAGEDEST = "/garage/rxState.php";

// Input Swich ports
const int closedSwitch = 2;
const int openSwitch = 0;

const byte cmdOFF[] = {0xA0, 0x01, 0x00, 0xA1};
const byte cmdON[] = {0xA0, 0x01, 0x01, 0xA2};

// Current time
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0; 
// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 2000;

unsigned long previousChangeTime = 0;

// Set web server port number to 80
WiFiServer server(80);

// Variable to store the HTTP request
String header;
int doorClosed = 0;
int doorOpen = 0;

int lastDoorClosed; 
int lastDoorOpen; 

String myIP = "";
String myName = "";

#define LOGSIZE 20

struct _logs {
  char output[50];
  char timeStamp[30];
}logArray[LOGSIZE];

struct _logs *currentLogPtr = logArray;
int currentLog = 0;

int failedToSend = 0;

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
	// Start hardware states
	pinMode(0, OUTPUT);
	pinMode(closedSwitch, INPUT_PULLUP);
	pinMode(openSwitch, INPUT_PULLUP);
	// start the time client
	timeClient.begin();
	// Call the Wifi routines
	startWifi();
	//Let Mnemonics we're on board
	updateWeb( remote_PAGEDEST, "BOOTING" );
	//Store the initial state of the doors
	lastDoorClosed = digitalRead(closedSwitch);
	lastDoorOpen = digitalRead(openSwitch);
	//add successful booting to the logs
	String bootString = "Successful Booting ";
	bootString += __version__;
	addLogEntry(bootString);
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
	int wait = 6000;
	long int time = millis();
	
	// ensure we are only a station
	WiFi.mode(WIFI_STA);
	
	// Set the wifi client name
	WiFi.hostname("broadleaf_Garage");

	// Start the wifi and connect to the Router
	WiFi.begin(ssid, password);
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
		addLogEntry("FAILED to get WiFi Connection - ABORTING!");
		return;
	}

	// store the variables
	myIP = WiFi.localIP().toString();
	myName = WiFi.hostname();  

	// log it
	addLogEntry("Connected to Router");

	// Now start the Server - for connecting to mnemonics
	server.begin();
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
  checkForChangeInDoorState();
  //check health of things
  checkHealth();
  
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
	if ( failedToSend >= 3 )
	{
		addLogEntry("Too many Failed to Sends...restarting Wifi");
		WiFi.disconnect();
		delay(100);
		startWifi();
		failedToSend = 0;
	}


}


/**
 * Name: checkForChangeInDoorState
 * 
 * Return: None
 * 
 * Parameters: None
 * 
 * Description: check if a switch has changed since last time
 *
 */
void checkForChangeInDoorState()
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
      updateWeb( remote_PAGEDEST, doorState );
      addLogEntry("Change of Door State Seen");
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
 * Name: addLogEntry
 * 
 * Return: Nothing
 * 
 * Parameters: String to add to log Struct
 * 
 * Description: adds a log entry, and time stamp to the log struct - overwritting in a loop
 *
 */
void addLogEntry(String output)
{
  if ( currentLog >= LOGSIZE-1 )
    currentLog = 0;
  else 
    currentLog++;
    
  output.toCharArray(currentLogPtr[currentLog].output, 50);
  getTimeStamp().toCharArray(currentLogPtr[currentLog].timeStamp, 30);
  
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
  WiFiClient client = server.available();   // Listen for incoming clients

  if (client) 
  {                                         // If a new client connects,
    String currentLine = "";                // make a String to hold incoming data from the client
    currentTime = millis();
    previousTime = currentTime;
       
    while (client.connected() && currentTime - previousTime <= timeoutTime) 
    {                                       // loop while the client's connected
      currentTime = millis();         
      if (client.available())
      {                                     // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        header += c;
        if (c == '\n') 
         {                                  // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) 
          {
            doorClosed = digitalRead(closedSwitch);
            doorOpen = digitalRead(openSwitch);
            
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();

            if (header.indexOf("GET /pulse") >= 0) 
            {
              activate();
              addLogEntry("Client request for PULSE");
            }

            if (  (header.indexOf("GET /close") >= 0) && doorOpen == LOW  ) 
            {
              activate();
              addLogEntry("Client request for CLOSE");
            }

            if (  (header.indexOf("GET /open") >= 0) && doorClosed == LOW   )
            {
              activate();
              addLogEntry("Client request for OPEN");
            }
            

            
            doorClosed = digitalRead(closedSwitch);
            
            // Display the HTML web page
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            // CSS to style the on/off buttons 
            // Feel free to change the background-color and font-size attributes to fit your preferences
            client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
            client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 40px;");
            client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
            client.println(".button2 {background-color: #77878A;}</style></head>");
            
            // Web Page Heading
            client.println("<body><a href=\"http://");
            client.println(myIP);
            client.println("/\"><h1>BROADLEAF GARAGE SERVER</a></h1>");
            
            if ( doorClosed == LOW ) { client.println("<body><h3>Currently CLOSED</h3>"); }

            else if ( doorOpen == LOW ) { client.println("<body><h3>Currently OPEN</h3>"); }

            else { client.println("<body><h3>Currently MOVING</h3>"); }
           
           
           if ( doorClosed == HIGH ) { client.println("<p>Switch1 = OPEN<p>"); }
           else { client.println("<p>Switch1 = CLOSED<p>"); }

           if ( doorOpen == HIGH ) { client.println("<p>Switch2 = OPEN<p>"); }
           else { client.println("<p>Switch2 = CLOSED<p>"); }
            
            client.println("<p><a href=\"/pulse\"><button class=\"button\">PULSE</button></a></p>");
            client.println("<p><a href=\"/open\"><button class=\"button\">OPEN</button></a></p>");
            client.println("<p><a href=\"/close\"><button class=\"button\">CLOSE</button></a></p>");
            
            if (  (header.indexOf("GET /logs") >= 0) )
            {
            for ( int i = 0; i < LOGSIZE; i++ )
            {
              client.println("<p>Output: ");
                client.println(currentLogPtr[currentLog].output);
                client.println(" - ");
                client.println(currentLogPtr[currentLog].timeStamp);
                client.println("<p>");
                
                currentLog--;
                if ( currentLog <= -1 )
                  currentLog = LOGSIZE-1 ;
            }
             
            }
            else
            {
              client.println("<p>Last Output: ");
              client.println(currentLogPtr[currentLog].output);
              client.println("</p>");
              client.println("<p>");
              client.println(currentLogPtr[currentLog].timeStamp);
              client.println("<p>");
              
              client.println("<p><a href=\"http://");
              client.println(myIP);
              client.println("/logs/\">View with all logs</a></p>");
              
              
          }
            client.println("</body></html>");
            // The HTTP response ends with another blank line
            client.println();
            // Break out of the while loop
            break;
          } else { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    // Clear the header variable
    header = "";
    // Close the connection
    client.stop();

  }
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
	Serial.write(cmdON, 4);
	delay(1000);
	Serial.write(cmdOFF,4);
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
		addLogEntry("Wifi Gone - Restarting Wifi");
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
	String host = remote_WEBADDRESS;

	if (!sender.connect(host, port)) 
	{
		// If we don't have a connection to mnemonics - log the failure and drop out
		addLogEntry("FAILED to connect to Mnemonics");
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
	addLogEntry(line);

	sender.stop();
}
