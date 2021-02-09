// Load Wi-Fi library

#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

const long utcOffsetInSeconds = 3600;

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "asia.pool.ntp.org", utcOffsetInSeconds);

char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};


// Replace with your network credentials
const char* ssid     = "XXXSSIDXXX";
const char* password = "XXXPASSWORD";

String remote_WEBADDRESS = "www.mnemonics.co.nz";
String remote_PAGEDEST = "/garage/rxState.php";


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

//void updateWeb(String pageDestination, String dataToSend)
//void checkForClient();
//void checkForChangeInDoorState();

void setup()
{
  pinMode(0, OUTPUT);
  pinMode(closedSwitch, INPUT_PULLUP);
  pinMode(openSwitch, INPUT_PULLUP);
  Serial.begin(9600);
  
  
  WiFi.hostname("broadleaf_Garage");


  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }
  
  myIP = WiFi.localIP().toString();
  myName = WiFi.hostname();
  
  updateWeb( remote_PAGEDEST, "BOOTING" );
  
  // Print local IP address and start web server
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  server.begin();
  
  timeClient.begin();
  
  lastDoorClosed = digitalRead(closedSwitch);
  lastDoorOpen = digitalRead(openSwitch);
  
  addLogEntry("Successful Booting");
  

}



void loop()
{
	checkForClient();
	
	checkForChangeInDoorState();
	
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
	/*timeStamp += timeClient.getHours();
	timeStamp += ":";
	timeStamp += timeClient.getMinutes();
	timeStamp += ":";
	timeStamp += timeClient.getSeconds();*/
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
    Serial.println("New Client.");          // print a message out in the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    currentTime = millis();
    previousTime = currentTime;
       
    
    while (client.connected() && currentTime - previousTime <= timeoutTime) 
    {                                       // loop while the client's connected
      currentTime = millis();         
      if (client.available())
      {                                     // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
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
    Serial.println("Client disconnected.");
    Serial.println("");
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
	//Serial.print("Wifi Status: " + String(WiFi.status()));
	String GET = pageDestination + "?state=" + dataToSend + "&sender=" + myIP + "-[" + myName + "]";
	//GET.replace(" ", "_");
	
	//Serial.println(GET);
	
	WiFiClient sender;
	
	const uint16_t port = 80;
	//const char * host = "mnemonics.co.nz"; // ip or dns
	String host = remote_WEBADDRESS;

	//if ( DEBUG >= DEBUG_LEVEL_1 )
		Serial.println("Attempting to send data to :[" + host + "]");

	if (!sender.connect(host, port)) 
	{
		//if ( DEBUG >= DEBUG_LEVEL_1 )
			Serial.println("connection failed");
			addLogEntry("FAILED to connect to Mnemonics");
		return;
	}

	// This will send the request to the server
	sender.print("GET " + GET);
	sender.print(" HTTP/1.1\r\nHost: ");
	sender.print(host);
	sender.print("\r\nConnection: close\r\n");
	sender.println();
	


	delay(1000);
	
	// Read back the first line of the response - Ideally: HTTP/1.1 200 OK
	String line = sender.readStringUntil('\r');
	Serial.println(line);
	
	addLogEntry(line);
	Serial.println("closing connection");
	
	sender.stop();
}
