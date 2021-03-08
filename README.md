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
