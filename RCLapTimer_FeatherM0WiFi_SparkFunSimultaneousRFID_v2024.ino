
/*
  Sketch for lap timing system using Simultaneous RFID Tag Reader
  By: Bruce Walker
  Date: February 2022

  This sketch does something...
*/

#define ENABLE_DEBUGGING false

// include SPI, MP3 and SD libraries
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <AceButton.h>
#include "arduino_secrets.h" 
#include <WiFi101.h>

#include <cUtils.h>
cUtils utils;

// **************************************************
// Items related time annoucement queue

  #define MAX_ANNOUNCE_TIME_QUEUE 3
  struct announcement
  {
    byte carnumber = NULL;
    int laptime = NULL;
  };
  announcement anAnnouceTimes[MAX_ANNOUNCE_TIME_QUEUE];
  byte nStoreAnnounceTimeIndex = 0;
  byte nAnnounceTimeIndex = 0;

// **************************************************

// **************************************************
// Items related to WiFi/SSLClient

///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = SECRET_SSID;        // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)

int wifi_status = WL_IDLE_STATUS;
boolean bWiFiActive = false;

// Initialize the Ethernet client library
// with the IP address and port of the server
// that you want to connect to (port 80 is default for HTTP):
WiFiSSLClient sslClient;
bool wifiAvailable = false;
bool wifiActive = false;
int wifiStatus = WL_IDLE_STATUS;
bool bConnected = false;

// server address:
char server[] = "bdwalker.net";

unsigned long lastConnectionTime = 0;            // last time you connected to the server, in milliseconds
const unsigned long postingInterval = 10L * 1000L; // delay between updates, in milliseconds

// **************************************************

// **************************************************
// Items for Feather Music Maker Shield

#include <Adafruit_VS1053.h>
  // These are the pins used
  #define VS1053_RESET    -1     // VS1053 reset pin (not used!)
  #define VS1053_CS       6     // VS1053 chip select pin (output)
  #define VS1053_DCS     10     // VS1053 Data/command select pin (output)
  #define CARDCS          5     // Card chip select pin
  // DREQ should be an Int pin *if possible* (not possible on 32u4)
  #define VS1053_DREQ     9     // VS1053 Data request, ideally an Interrupt pin

  // Create the audio player object we'll use
  Adafruit_VS1053_FilePlayer audioPlayer = 
    Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, CARDCS);

  uint8_t nAudioVolume = 16;

  // Audio File Queue variables
  bool bWasPlaying = false;
  String strBlank = "";
  #define MAX_QUEUED_AUDIO_FILES MAX_ANNOUNCE_TIME_QUEUE * 10
  String strAudioFile[MAX_QUEUED_AUDIO_FILES];
  byte nQueueFileIndex = 0;
  byte nPlayFileIndex = 0;
  
  // Audio File Queue functions
  void doAnnouncements();
  bool queueAudioFile( String strFileToQueue );
  String getAudioFilenameForNumber( int nNumber );
  void queueAnnouncement( int nLapTime );
  
// **************************************************

// **************************************************
// Items for SparkFun Simultaneous RFID Reader

#include "SparkFun_UHF_RFID_Reader.h" //Library for controlling the M6E Nano module
  
  // #define RFID_DEBUG_ENABLED   // Uncomment this line to enable RFID debugging messages

  // These are the pins used
  #define RFID_RX_PIN 0
  #define RFID_TX_PIN 1
  #define BUZZER_PIN 13
  #define RFID_ENABLE_PIN 16

  // Create the RFID reader object "nano"
  RFID nano; //Create RFID reader instance

  bool bRFIDEnabled = false;
  bool bRFIDReading = false;
  bool bRFIDPaused = false;
  byte nRFIDReadPower = 14;
  
// **************************************************

// **************************************************
// Items for OLED display

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Also setup buttone
#define BUTTON_UP_PIN 15
#define BUTTON_DOWN_PIN 14

using namespace ace_button;
AceButton btnUp( BUTTON_UP_PIN );
AceButton btnDown( BUTTON_DOWN_PIN );

// Forward reference to prevent Arduino compiler becoming confused.
void handleEvent(AceButton*, uint8_t, uint8_t);

uint32_t nLastButtonProcessTime = 0;

// **************************************************

#define CAR_NUMBER_DISPLAY_COUNT 6
byte carnumberlist[CAR_NUMBER_DISPLAY_COUNT];

struct racer {
  byte carnumber = 255;
  char racername[12] = "";
  byte laps = 0;
  uint32_t lastlap = 999999999;
  uint32_t fastlap = 999999999;
  byte lastrssi = 100;
  uint32_t firstlapstarttime = 0;
  uint32_t lastcompletedlaptime = 0;
  uint32_t lastpasstime = 0;
};

#define RACER_EXPIRATION_MINUTES 2
#define MIN_LAP_TIME_SECS 3
#define MAX_LAP_TIME_SECS 99
#define MAX_RACERS 20
racer racerLaps[MAX_RACERS];

#define RACER_RECORD_UPLOAD_INTERVAL_SECS 5
long nLastUploadMS = 0;

void setup()
{
  Serial.begin(115200);

  randomSeed( analogRead(A2) );

  // Prepare buzzer pin
  pinMode( BUZZER_PIN, OUTPUT );
  digitalWrite( BUZZER_PIN, LOW );
  lowBeep(); // Test tone

  // Init announce time queue
  for (int i=0; i<MAX_ANNOUNCE_TIME_QUEUE; i++)
  {
    anAnnouceTimes[i].carnumber = NULL;
    anAnnouceTimes[i].laptime = NULL;
  }
  
  // Init audio file queue
  for (int i=0; i<MAX_QUEUED_AUDIO_FILES; i++)
  {
    strAudioFile[i] = "";
  }

  // Init laptime array and car display list
  resetAllRacers();

  // ********************************************************************************
  // Setup for OLED Display

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }

  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  display.clearDisplay();
  // display.invertDisplay(true);
  display.display();

  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.cp437(true);         // Use full 256 char 'Code Page 437' font

  displayRFIDReadPower();
  displayAudioVolume();

//  display.setTextSize(1);
//  display.fillRect(0, 17, display.width(), display.height()-16, SSD1306_BLACK);
//  display.setCursor(0, 18);
//  display.println( "Car: 01" );
//  display.println( "Car: 02" );
//  display.println( "Car: 03" );
//  display.println( "Car: 04" );
//  display.println( "Car: 05" );
//  display.display();

  pinMode( BUTTON_UP_PIN, INPUT_PULLUP);
  pinMode( BUTTON_DOWN_PIN, INPUT_PULLUP);

  // Configure the ButtonConfig with the event handler, and enable all higher
  // level events.
  ButtonConfig* buttonConfig = ButtonConfig::getSystemButtonConfig();
  buttonConfig->setDebounceDelay(30);
  buttonConfig->setClickDelay(275);
  buttonConfig->setLongPressDelay(800);
  buttonConfig->setEventHandler(buttonHandler2);
  buttonConfig->setFeature(ButtonConfig::kFeatureClick);
//  buttonConfig->setFeature(ButtonConfig::kFeatureDoubleClick);
  buttonConfig->setFeature(ButtonConfig::kFeatureLongPress);
//  buttonConfig->setFeature(ButtonConfig::kFeatureRepeatPress);

  // ********************************************************************************
  
  // ********************************************************************************
  // Setup steps for Music Maker shield
  
  if (! audioPlayer.begin()) { // initialise the music player
     Serial.println(F("Couldn't find VS1053, do you have the right pins defined?"));
     while (1);
  }

  Serial.println(F("VS1053 found"));

  audioPlayer.sineTest(0x44, 500);    // Make a tone to indicate VS1053 is working
  
  if (!SD.begin(CARDCS)) {
    Serial.println(F("SD failed, or not present"));
    while (1);  // don't do anything more
  }
  Serial.println("SD OK!");
  
  // Set volume for left, right channels. lower numbers == louder volume!
  setAudioVolume( nAudioVolume );
  
  // If DREQ is on an interrupt pin we can do background
  // audio playing
  audioPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);  // DREQ int

  // ********************************************************************************

  // ********************************************************************************
  // Setup steps for RFID reader

  pinMode( RFID_ENABLE_PIN, OUTPUT );
  digitalWrite( RFID_ENABLE_PIN, HIGH );
  bRFIDEnabled = true;
  
  //Because we are using a hardware serial port in this example we can
  //push the serial speed to 115200bps
  while (setupNano(115200) == false) //Configure nano to run at 115200bps
  {
    Serial.println(F("Module failed to respond. Please check wiring."));
    displayMessage( "RFID Mod Err" );
    delay(2000); //Pause and try again
  }
  displayMessage( "RFID Start" );

  nano.setRegion(REGION_NORTHAMERICA); //Set to North America

  setRFIDReadPower( nRFIDReadPower );
  nano.setWritePower(500);

  // ********************************************************************************

//  wifi_setup();
}

void wifi_setup() {
  // Configure pins for Adafruit ATWINC1500 Feather
  WiFi.setPins(8,7,4,2);

  // check for the presence of the shield:
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    // Set flag to disable WiFi functions
    wifiAvailable = false;
  }
  else
  {
    wifiAvailable = true;
  }

  // by default the local IP address of will be 192.168.1.1
  // you can override it with the following:
  // WiFi.config(IPAddress(10, 0, 0, 1));

  if (wifiAvailable)
  {
    // attempt to connect to WiFi network:
    byte nAttempts = 0;
    while ( (nAttempts++ < 3) && (wifiStatus != WL_CONNECTED) ) {
      Serial.print("Attempting to connect to SSID: ");
      Serial.println(ssid);
      // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
      wifiStatus = WiFi.begin(ssid, pass);
  
      // wait 10 seconds for connection:
      long nStartMS = millis();
      while ( (utils.secsElapsedSince(nStartMS) < 10) && (wifiStatus != WL_CONNECTED) )
      {
        delay(500);
        wifiStatus = WiFi.status();
      }
    }
    
    if (wifiStatus == WL_CONNECTED) 
    {
      wifiActive = true;
      Serial.println("Connected to wifi");
      printWiFiStatus();
    
    }
    else
    {
      wifiActive = false;
      Serial.println("Failed to connect to WiFi. Wifi functions disabled.");
    }
  }
  else
  {
    // Set flag to enable WiFi functions
    bWiFiActive = false;
  }
}

void loop()
{
  if (Serial)
  {
    processSerialCommands();
  }

  if ( (!bRFIDReading) && (!bRFIDPaused) )
  {
    bRFIDReading = true;
    nano.startReading();
  }
  if ( (bRFIDReading) && (bRFIDPaused) )
  {
    bRFIDReading = false;
    nano.stopReading();
  }

  if (bRFIDReading)
  {
    checkForRFIDData();
    computeLapTimes();
  }
  doAnnouncements();
  processButtons();
  showRacerRecords();
//  uploadRacerRecords();
//  showSSLClientResponses();
}

void processButtons()
{
    btnUp.check();
    btnDown.check();
}

void buttonHandler2(AceButton* button, uint8_t eventType, uint8_t buttonState) {

  int btnPin = button->getPin();

//  // Print out a message for all events, for both buttons.
//  Serial.print(F("buttonHandler2(): pin: "));
//  Serial.print(btnPin);
//  Serial.print(F("; eventType: "));
//  Serial.print(AceButton::eventName(eventType));
//  Serial.print(F("; buttonState: "));
//  Serial.println(buttonState);

  switch (eventType) {
    case AceButton::kEventClicked:
      if (btnPin == BUTTON_UP_PIN) 
      {
        if (nRFIDReadPower < 27)
        {
          nRFIDReadPower++;
          setRFIDReadPower( nRFIDReadPower );
        }
      }
      if (btnPin == BUTTON_DOWN_PIN) 
      {
        if (nRFIDReadPower > 5)
        {
          nRFIDReadPower--;
          setRFIDReadPower( nRFIDReadPower );
        }
      }
      break;
    case AceButton::kEventLongPressed:
      if (btnPin == BUTTON_UP_PIN) 
      {
        if (nAudioVolume < 20)
        {
          nAudioVolume++;
          setAudioVolume( nAudioVolume );
        }
      }
      if (btnPin == BUTTON_DOWN_PIN) 
      {
        if (nAudioVolume > 1)
        {
          nAudioVolume--;
          setAudioVolume( nAudioVolume );
        }
      }
      break;
  }
}

void debugMessage( String strMessage, bool bCRAfter = true )
{
  if (ENABLE_DEBUGGING)
  {
    Serial.print( strMessage );
    if ( bCRAfter )
    {
      Serial.println();
    }

  }
}
void processSerialCommands()
{
  bool bCmdValid = true;
  if (Serial.available())
  {
    String strInput = "";
    char chrInput;
    bool bLineRead = false;
    while( Serial.available() && !bLineRead )
    {
      chrInput = Serial.read();
      switch (chrInput)
      {
        case 10:
          // Ignore line feeds
          break;
        case 13:
          // Carriage return indicates end of line
          bLineRead = true;
          break;
        default:
          if ( ( (chrInput >= 48) && (chrInput <= 57) ) || ( (chrInput >= 65) && (chrInput <= 90) ) || ( (chrInput >= 97) && (chrInput <= 122) ) )
          {
            strInput += String( chrInput );
          }
      }
    }
    debugMessage( strInput, true );
    char chrCommand = strInput.charAt(0);
    byte bCommand = checkSerialForCommand();
    switch (chrCommand)
    {
      case 'm':
        if ( strInput.length() == 1 )
        {
          Serial.println( "Commands:" );
          Serial.println( " p - Pause scanning" );
          Serial.println( " r - Resume scanning" );
          Serial.println( " w##AAAAAAAAAA - WriteTag: ## = car number, AAAAAAAAAA = name" );
          Serial.println( " ap## - Adjust scanning power: ## is a number between 5 and 27" );
          Serial.println( " av## - Adjust volume: ## is a number between 1 and 10" );
          Serial.println( " x - Reset all racer records" );
          Serial.println( " u - toggle web uploads" );
          Serial.println( "" );
        }
        else
        {
          bCmdValid = false;
        }
        break;
      case 'p':
        if ( strInput.length() == 1 )
        {
          bRFIDPaused = true;
          bRFIDReading = false;
          nano.stopReading();
          Serial.println( "RFID scanning paused." );
        }
        else
        {
          bCmdValid = false;
        }
        break;
      case 'r':
        if ( strInput.length() == 1 )
        {
          bRFIDPaused = false;
          Serial.println( "RFID scanning resumed." );
        }
        else
        {
          bCmdValid = false;
        }
        break;
      case 'x':
        if ( strInput.length() == 1 )
        {
          resetAllRacers();
        }
        else
        {
          bCmdValid = false;
        }
        break;
      case 'u':
        if ( strInput.length() == 1 )
        {
          if (wifiAvailable)
          {
            wifiActive = !wifiActive;
            Serial.print( "Uploads " );
            if (wifiActive)
            {
              Serial.println( "enabled." );
            }
            else
            {
              Serial.println( "disabled." );
            }
          }
          else
          {
            Serial.println( "WiFi unavailable." );
          }
        }
        else
        {
          bCmdValid = false;
        }
        break;
      case 'w':
        if (!bRFIDPaused)
        {
          Serial.println("ERROR: You need to pause scanning to write a tag.");
        }
        else
        {
          if ( (strInput.length() > 3) && (strInput.length() < 13) )
          {
            if ( isDigit( strInput.charAt(1)) && isDigit(strInput.charAt(2)) )
            {
              byte bCarNumber = strInput.substring(1,3).toInt();
              String strName = strInput.substring(3);
              writeRFIDTag( bCarNumber, strName );
            }
          }
          else
          {
            bCmdValid = false;
          }
        }
        break;
      case 'a':
        if ( strInput.length() > 2 )
        {
          char chrSubCommand = strInput.charAt(1);
          switch (chrSubCommand)
          {
            case 'p':
            {
              int nPower = strInput.substring(2).toInt();
              if ( (nPower>=5) && (nPower<=27) )
              {
                setRFIDReadPower( nPower );
              }
              else
              {
                bCmdValid = false;
              }
              break;
            }
            case 'v':
            {
              uint8_t nVolume = strInput.substring(2).toInt();
              if ( (nVolume>=1) && (nVolume<=20) )
              {
                setAudioVolume(nVolume);
              }
              else
              {
                bCmdValid = false;
              }
              break;
            }
            default:
            {
              bCmdValid = false;
              break;
            }
          }
        }
        else
        {
          bCmdValid = false;
        }
        break;
      default:
        // do nothing
        bCmdValid = false;
        break;
    }
  }
  if (!bCmdValid)
  {
    Serial.println();
    Serial.println( "Error: Command invalid!" );
  }
}

void setRFIDReadPower( int nPower )
{
  if (!bRFIDPaused)
    nano.stopReading();
  delay(250);
  nano.setReadPower( nPower * 100 );
  delay(250);
  if (!bRFIDPaused)
    nano.startReading();
  nRFIDReadPower = nPower;
  displayRFIDReadPower();
  Serial.print( "RFID read power set to " );
  Serial.println( String(nRFIDReadPower) );
}

bool writeRFIDTag( byte bCarNumber, String strName )
{
  char strEPC[] = "            "; //You can only write even number of bytes, we'll write a full 12
  strEPC[0] = bCarNumber;
  for (int i=0; i<strName.length(); i++)
  {
    strEPC[i+1] = strName.charAt(i);
  }
  byte responseType = nano.writeTagEPC(strEPC, sizeof(strEPC) - 1); //The -1 shaves off the \0 found at the end of string

  if (responseType == RESPONSE_SUCCESS)
  {
    Serial.print( "RFID Tag Written - Car:" );
    Serial.print( String(bCarNumber) );
    Serial.print( " Name:" );
    Serial.println( strName );
    return true;
  }
  else
  {
    Serial.println("Error: Failed towrite RFID Tag!");
  }
  return false;
}

byte elapsedMinutes( uint32_t since )
{
  byte elapsedMins = 0;
  elapsedMins = (byte) ( elapsedMillis( since ) / 60000 );
  return elapsedMins;
}

byte elapsedSeconds( uint32_t since )
{
  byte elapsedSecs = 0;
  elapsedSecs = (byte) ( elapsedMillis( since ) / 1000 );
  return elapsedSecs;
}

uint32_t elapsedMillis( uint32_t since )
{
  
  return (uint32_t) millis() - since; 
}

void computeLapTimes()
{
  for (int i=0; i<MAX_RACERS; i++)
  {
    if (racerLaps[i].carnumber != 255)
    {
      // If RACER_EXPIRATION_MINUTES minutes have elapsed since last recorded time, reset the record.
      if ( (racerLaps[i].lastcompletedlaptime > 0) && ( elapsedMinutes( racerLaps[i].lastcompletedlaptime ) > RACER_EXPIRATION_MINUTES ) )
      {
        resetRacer( i );
        break;
      }
      // if it has been 500ms since last RFID read, count that as completed lap
      if ( (racerLaps[i].lastcompletedlaptime != racerLaps[i].lastpasstime) && (elapsedMillis( racerLaps[i].lastpasstime ) >= 500) )
      {
        // If this is their first reading, set first lap start time 
        if ( racerLaps[i].firstlapstarttime == 0 )
        {
          // Reset RSSI for next pass
          racerLaps[i].lastrssi = 100;
          racerLaps[i].firstlapstarttime = racerLaps[i].lastpasstime;
          racerLaps[i].lastcompletedlaptime = racerLaps[i].lastpasstime;
          Serial.print( "Car: " );
          Serial.print( String(racerLaps[i].carnumber) );
          Serial.println( " First pass" );
          displayCarNumber( racerLaps[i].carnumber );
        }
        else
        {
          if ( ( (racerLaps[i].lastpasstime - racerLaps[i].lastcompletedlaptime) / 1000 ) > MIN_LAP_TIME_SECS )
          {
            // Reset RSSI for next pass
            racerLaps[i].lastrssi = 100;
            racerLaps[i].laps += 1;
            racerLaps[i].lastlap = (racerLaps[i].lastpasstime - racerLaps[i].lastcompletedlaptime) / 10;
            if ( racerLaps[i].lastlap < racerLaps[i].fastlap )
            {
              racerLaps[i].fastlap = racerLaps[i].lastlap;
            }
            Serial.print( "Car: " );
            Serial.print( String(racerLaps[i].carnumber) );
            Serial.print( " Laps: " );
            Serial.print( String(racerLaps[i].laps) );
            Serial.print( " Fast Lap: " );
            Serial.print( String(racerLaps[i].fastlap / 100) );
            Serial.print( "." );
            if ( (racerLaps[i].fastlap % 100) < 10 )
              Serial.print( "0" );
            Serial.print( String(racerLaps[i].fastlap % 100) );
            Serial.print( " Last Lap: " );
            Serial.print( String(racerLaps[i].lastlap / 100) );
            Serial.print( "." );
            if ( (racerLaps[i].lastlap % 100) < 10 )
              Serial.print( "0" );
            Serial.print( String(racerLaps[i].lastlap % 100) );
            Serial.println( "" );
            racerLaps[i].lastcompletedlaptime = racerLaps[i].lastpasstime;
            if ( (racerLaps[i].lastlap/100) <= MAX_LAP_TIME_SECS )
            {
              queueTimeToAnnounce( racerLaps[i].carnumber, racerLaps[i].lastlap );
            }
            displayCarNumber( racerLaps[i].carnumber );
          }
        }
      }
    }
  }
}

void resetAllRacers()
{
  for (int i=0; i<MAX_RACERS; i++)
  {
    resetRacer(i);
  }
  // Reset car number display list
  for ( int i=0; i<CAR_NUMBER_DISPLAY_COUNT; i++ )
  {
    carnumberlist[i] = 255;
  }
}

String allRacerRecords()
{
  String strAllRacerRecords = "";
  for (int racerIndex=0; racerIndex<MAX_RACERS; racerIndex++)
  {
    if (racerLaps[racerIndex].carnumber != 255)
    {
      String strRacerRecord = "";
      strRacerRecord += String(racerLaps[racerIndex].carnumber);
      strRacerRecord += ",";
      strRacerRecord += String(racerLaps[racerIndex].laps);
      strRacerRecord += ",";
      strRacerRecord += String(racerLaps[racerIndex].lastlap);
      strRacerRecord += ",";
      strRacerRecord += String(racerLaps[racerIndex].fastlap);
      strRacerRecord += "\n";
      strAllRacerRecords += strRacerRecord;
    }
  }
  if (strAllRacerRecords.length() > 0)
  {
    Serial.println(strAllRacerRecords);
  }
  return strAllRacerRecords;
}

void showRacerRecords()
{
  if ( utils.secsElapsedSince( nLastUploadMS ) > RACER_RECORD_UPLOAD_INTERVAL_SECS )
  {
    nLastUploadMS = millis();
    bool bActiveRacers = false;
    for (int racerIndex=0; racerIndex<MAX_RACERS; racerIndex++)
    {
      if (racerLaps[racerIndex].carnumber != 255)
      {
        bActiveRacers = true;
        break;
      }
    }
    if (bActiveRacers)
    {
      String strAllRacerRecords = "";
      Serial.println( " CAR   LAP COUNT   LAST LAP   FAST LAP");
      Serial.println( "----- ----------- ---------- ----------");
      
      for (int racerIndex=0; racerIndex<MAX_RACERS; racerIndex++)
      {
        if (racerLaps[racerIndex].carnumber != 255)
        {
          String strRacerRecord = centerString(String(racerLaps[racerIndex].carnumber), 5);
          strRacerRecord += " ";
          strRacerRecord += centerString(String(racerLaps[racerIndex].laps), 11);
          strRacerRecord += " ";
          if ( racerLaps[racerIndex].laps == 0 )
          {
            strRacerRecord += centerString("OUT LAP", 10);
            strRacerRecord += " ";
            strRacerRecord += centerString(" ", 10);
          }
          else
          {
            strRacerRecord += centerString(formatLapTime(racerLaps[racerIndex].lastlap), 10);
            strRacerRecord += " ";
            strRacerRecord += centerString(formatLapTime(racerLaps[racerIndex].fastlap), 10);
          }
          strRacerRecord += "\n";
          strAllRacerRecords += strRacerRecord;
        }
      }
      Serial.print( strAllRacerRecords );
      Serial.println( "----- ----------- ---------- ----------\n");
    }
  }
}

String centerString( String str, byte nLength )
{
  String strCentered = "";  
  if (str.length() >= nLength)
    strCentered = str.substring(0, nLength);
  else
  {
    byte pad = nLength - str.length();
    pad = pad / 2;
    for (int i=0; i<pad; i++)
    {
      strCentered += " ";
    }
    strCentered += str;
    for (int i=0; i<(pad+1); i++)
    {
      strCentered += " ";
    }
    strCentered = strCentered.substring(0, nLength);
    
  }
  return strCentered;
}

String formatLapTime( int laptime )
{
  String strLapTimeFormated = "";
  strLapTimeFormated += String(laptime / 100);
  strLapTimeFormated += ".";
  if ( (laptime % 100) < 10 )
    strLapTimeFormated += "0";
  strLapTimeFormated += String(laptime % 100);
  return strLapTimeFormated;
}

void uploadRacerRecords() 
{
  if ( wifiActive && ( utils.secsElapsedSince( nLastUploadMS ) > RACER_RECORD_UPLOAD_INTERVAL_SECS ) )
  {
    nLastUploadMS = millis();
    
    // close any connection before send a new request.
    // This will free the socket on the WiFi shield
    sslClient.stop();
  
    Serial.println("\nStarting connection to server...");
    // if you get a connection, report back via serial:
    if (sslClient.connect(server, 443)) {
      Serial.println("connected to server");
      bConnected = true;
      // Make a HTTP request:
      sslClient.println("PUT /rclaptimes/racerdataraw.csv text/plain");
      sslClient.println("Host: bdwalker.net");
      sslClient.println("User-Agent: ArduinoWiFi/1.1");
      sslClient.println("Connection: close");
      sslClient.println();
      sslClient.println( allRacerRecords() );
      sslClient.println();

    }
    else {
      // if you couldn't make a connection:
      Serial.println("connection failed");
    }
  }
}

void showSSLClientResponses()
{
      while (sslClient.available())
      {
        char c = sslClient.read();
        Serial.write(c);
      }
}


void resetRacer( byte racerIndex )
{
  Serial.print( "Purging car number " );
  Serial.println( String(racerLaps[racerIndex].carnumber) );
  racerLaps[racerIndex].carnumber = 255;
  char blankname[12] = "";
  memcpy( racerLaps[racerIndex].racername, blankname, sizeof(racerLaps[racerIndex].racername) );
  racerLaps[racerIndex].laps = 0;
  racerLaps[racerIndex].firstlapstarttime = 0;
  racerLaps[racerIndex].lastlap = 999999999;
  racerLaps[racerIndex].fastlap = 999999999;
  racerLaps[racerIndex].lastrssi = 100;
  racerLaps[racerIndex].lastpasstime = 0;
  racerLaps[racerIndex].lastcompletedlaptime = 0;
  return;
}

byte getIndexForCar( byte carnumber )
{
  byte index = 255;
  for (int i=0; i<MAX_RACERS; i++)
  {
    if ( racerLaps[i].carnumber == carnumber )
    {
      index = i;
      break;
    }
  }
  if ( index == 255 )
  {
    for (int i=0; i<MAX_RACERS; i++)
    {
      if ( racerLaps[i].carnumber == 255 )
      {
        index = i;
        racerLaps[i].carnumber = carnumber;
        break;
      }
    }
  }
  return index;
}

char checkSerialForCommand()
{
  char command = 0;
  if (Serial.available())
  {
    command = Serial.read();
  }
  return command;
}

void checkForRFIDData()
{
  if (nano.check() == true) //Check to see if any new data has come in from module
  {
    byte responseType = nano.parseResponse(); //Break response into tag ID, RSSI, frequency, and timestamp

    if (responseType == RESPONSE_IS_KEEPALIVE)
    {
      debugMessage(F("Scanning for RFID"));
    }
    else if (responseType == RESPONSE_IS_TAGFOUND)
    {
      uint32_t rfidReadTime = millis();
      
      //If we have a full record we can pull out the fun bits
      int rssi = nano.getTagRSSI(); //Get the RSSI for this tag read

      long freq = nano.getTagFreq(); //Get the frequency this tag was detected at

      long timeStamp = nano.getTagTimestamp(); //Get the time this was read, (ms) since last keep-alive message

      byte tagEPCBytes = nano.getTagEPCBytes(); //Get the number of bytes of EPC from response

      if (ENABLE_DEBUGGING)
      {
        String strRFIDInfo = F(" rssi[");
        strRFIDInfo += String( rssi );
        strRFIDInfo += F("] freq[");
        strRFIDInfo += String( freq );
        strRFIDInfo += F("] time[");
        strRFIDInfo += String( timeStamp );
        strRFIDInfo += F("] car[");
        strRFIDInfo += String( nano.msg[31] );
        strRFIDInfo += F("] name[");
        String strName = "";
        for (byte x=1; x < tagEPCBytes; x++)
        {
          strName += String( char( nano.msg[31 + x] ) );
          strName.trim();
        }
        strRFIDInfo += strName;
        strRFIDInfo += F("]");

        debugMessage( strRFIDInfo, true );
      }

      byte nCarIndex = getIndexForCar( nano.msg[31] );
      if ( nCarIndex != 255 )
      {
        if (abs(rssi) < racerLaps[nCarIndex].lastrssi)
        {
          racerLaps[nCarIndex].lastpasstime = rfidReadTime;
          
        }
        racerLaps[nCarIndex].lastrssi = abs(rssi);
      }
    }
    else if (responseType == ERROR_CORRUPT_RESPONSE)
    {
//      Serial.println("Bad CRC");
    }
    else
    {
      //Unknown response
//      Serial.print("Unknown error");
    }
  }
}

void setAudioVolume( uint8_t nVol )
{
  uint8_t nSetVol = 20 - nVol;
  audioPlayer.setVolume(nSetVol,nSetVol);
  nAudioVolume = nVol;
  displayAudioVolume();
  Serial.print( "Volume set to " );
  Serial.println( String(nAudioVolume) );
}

void displayAudioVolume()
{
  display.setTextSize(2);
  display.fillRect(64, 0, 64, 16, SSD1306_BLACK);
  display.setCursor(64, 0);    
  display.print( "V:" );
  if ((nAudioVolume/10) == 0 )
  {
    display.print( " " );
  }
  display.print( nAudioVolume );
  display.display();
}

void displayRFIDReadPower()
{
  display.setTextSize(2);
  display.fillRect(0, 0, 64, 16, SSD1306_BLACK);
  display.setCursor(0, 0);     
  display.print( "P:" );
  if ((nRFIDReadPower/10) == 0 )
  {
    display.print( " " );
  }
  display.print( nRFIDReadPower );
  display.display();
}

void displayCarNumber( byte nCarNumber )
{
  // Place new car number into list
  for ( int i=CAR_NUMBER_DISPLAY_COUNT; i>0; i-- )
  {
    carnumberlist[i] = carnumberlist[i-1];
  }
  carnumberlist[0] = nCarNumber;
  display.setTextSize(2);
  display.fillRect(0, 17, display.width(), display.height()-16, SSD1306_BLACK);
  display.setCursor(8, 22);
  int nIndex = 0;
  while ( (nIndex < CAR_NUMBER_DISPLAY_COUNT) && (carnumberlist[nIndex] != 255) )
  {
    if ( carnumberlist[nIndex] < 10 )
    {
      display.print( "0" );
    }
    display.print( carnumberlist[nIndex] );
    display.print( " " );
    nIndex++;
    if ( nIndex == 3 )
    {
      display.setCursor(8, 42);
    }
  }
  display.display();
}

void displayMessage( String strMsg )
{
  display.setTextSize(2);
  display.fillRect(0, 17, display.width(), display.height()-16, SSD1306_BLACK);
  display.setCursor(8, 22);
  display.print( strMsg );
  display.display();
}

void doAnnouncements()
{
  if (audioPlayer.stopped())
  {
    if (bWasPlaying)
    {
      bWasPlaying = false;
      strAudioFile[nPlayFileIndex] = "";
      if ( ++nPlayFileIndex == MAX_QUEUED_AUDIO_FILES )
        nPlayFileIndex = 0;
    }
    if (strAudioFile[nPlayFileIndex] == "")
    {
      announcement anNextTime = fetchTimeToAnnounce();
      if ( anNextTime.carnumber != NULL )
      {
        queueAnnouncement( anNextTime );
      }
    }
    if (strAudioFile[nPlayFileIndex] != "")
    {
      debugMessage( "Starting: ", false );
      debugMessage( strAudioFile[nPlayFileIndex] );
      char strFilename[14] = "";
      strAudioFile[nPlayFileIndex].toCharArray( strFilename, 14 );
      audioPlayer.startPlayingFile( strFilename );
      bWasPlaying = true;
    }
  }
}

void queueAnnouncement( announcement anLapTime )
{
    int nSecs = anLapTime.laptime / 100;
    int nDecimal = anLapTime.laptime % 100;
  
    Serial.print( "Announcing - Car: " );
    Serial.print( String(anLapTime.carnumber) );
    Serial.print( " " );
    Serial.print( String(nSecs) );
    Serial.print( "." );
    if (nDecimal < 10)
      Serial.print( "0" );
    Serial.print(  String(nDecimal) );
    Serial.println( "" );

    char strPlayFile[14] = "";
    String strAudioFilename = "car.mp3";
    strAudioFilename.toCharArray( strPlayFile, 14 );
    if ( queueAudioFile( strPlayFile ) )
    {
      strAudioFilename = getAudioFilenameForNumber( anLapTime.carnumber );
      strAudioFilename.toCharArray( strPlayFile, 14 );
      queueAudioFile( strPlayFile );

      if ( nSecs > 100 )
      {
        strAudioFilename = getAudioFilenameForNumber( nSecs / 100 );
        strAudioFilename.toCharArray( strPlayFile, 14 );
        queueAudioFile( strPlayFile );
        strAudioFilename = "Hundred.mp3";
        strAudioFilename.toCharArray( strPlayFile, 14 );
        queueAudioFile( strPlayFile );
        nSecs = nSecs % 100;
      }
      
      strAudioFilename = getAudioFilenameForNumber( nSecs) ;
      strAudioFilename.toCharArray( strPlayFile, 14 );
      queueAudioFile( strPlayFile );
      strAudioFilename = "point.mp3";
      strAudioFilename.toCharArray( strPlayFile, 14 );
      queueAudioFile( strPlayFile );
      strAudioFilename = getAudioFilenameForNumber( nDecimal / 10 );
      strAudioFilename.toCharArray( strPlayFile, 14 );
      queueAudioFile( strPlayFile );
      if ( (nDecimal % 10) != 0 )
      {
        strAudioFilename = getAudioFilenameForNumber( nDecimal % 10 );
        strAudioFilename.toCharArray( strPlayFile, 14 );
        queueAudioFile( strPlayFile );
      }
      else
      {
        strAudioFilename = "PauseS.mp3";
        strAudioFilename.toCharArray( strPlayFile, 14 );
        queueAudioFile( strPlayFile );
      }
    }
}

String getAudioFilenameForNumber( int nNumber )
{
  String strFilename = String(nNumber);
    while (strFilename.length() < 4)
      strFilename = "0" + strFilename;
    strFilename += ".mp3";
    
  return strFilename;
}

bool queueAudioFile( String strFileToQueue )
{
  if ( strFileToQueue.length() <= 14 )
  {
    if ( strAudioFile[nQueueFileIndex] == "" )
    {
      strAudioFile[nQueueFileIndex] = strFileToQueue;
      if ( ++nQueueFileIndex == MAX_QUEUED_AUDIO_FILES )
        nQueueFileIndex = 0;
      return true;
    }
  }
  Serial.println();
  Serial.println( "BAD, BAD BAD!" );
  Serial.println( "BAD, BAD BAD!" );
  return false;
}

bool queueTimeToAnnounce( byte bCarNumber, int nLapTime )
{
  if ( anAnnouceTimes[nStoreAnnounceTimeIndex].carnumber == NULL )
  {
    anAnnouceTimes[nStoreAnnounceTimeIndex].carnumber = bCarNumber;
    anAnnouceTimes[nStoreAnnounceTimeIndex].laptime = nLapTime;
    if ( ++nStoreAnnounceTimeIndex == MAX_ANNOUNCE_TIME_QUEUE )
      nStoreAnnounceTimeIndex = 0;
    return true;
  }
  Serial.print( " -- No room to announce time!" );
  return false;
}

announcement fetchTimeToAnnounce()
{
  announcement anOutput;
  if ( anAnnouceTimes[nAnnounceTimeIndex].carnumber != NULL )
  {
    anOutput.carnumber = anAnnouceTimes[nAnnounceTimeIndex].carnumber;
    anOutput.laptime = anAnnouceTimes[nAnnounceTimeIndex].laptime;
    anAnnouceTimes[nAnnounceTimeIndex].carnumber = NULL;
    anAnnouceTimes[nAnnounceTimeIndex].laptime = NULL;
    if ( ++nAnnounceTimeIndex == MAX_ANNOUNCE_TIME_QUEUE )
      nAnnounceTimeIndex = 0;
  }
//  Serial.println();
//  Serial.println( "No times to announce!" );
  return anOutput;
}

//Gracefully handles a reader that is already configured and already reading continuously
//Because Stream does not have a .begin() we have to do this outside the library
boolean setupNano(long baudRate)
{
#ifdef RFID_DEBUG_ENABLED
  nano.enableDebugging(Serial); //Print the debug statements to the Serial port
#endif
  
  // Tell the library which serial port to communicate over
  nano.begin(Serial1); 

  //Test to see if we are already connected to a module
  //This would be the case if the Arduino has been reprogrammed and the module has stayed powered
  Serial1.begin(baudRate); //For this test, assume module is already at our desired baud rate
  while(!Serial1);

  //About 200ms from power on the module will send its firmware version at 115200. We need to ignore this.
  while (Serial1.available()) Serial1.read();

  nano.getVersion();

  if (nano.msg[0] == ERROR_WRONG_OPCODE_RESPONSE)
  {
    //This happens if the baud rate is correct but the module is doing a ccontinuous read
    nano.stopReading();
    Serial.println(F("Module continuously reading. Asking it to stop..."));
    delay(1500);
  }
  else
  {
    //The module did not respond so assume it's just been powered on and communicating at 115200bps
    Serial1.begin(115200); //Start serial at 115200
    nano.setBaud(baudRate); //Tell the module to go to the chosen baud rate. Ignore the response msg
    Serial1.begin(baudRate); //Start the serial port, this time at user's chosen baud rate
    delay(250);
  }

  //Test the connection
  nano.getVersion();

  if (nano.msg[0] != ALL_GOOD) return (false); //Something is not right

  //The M6E has these settings no matter what
  nano.setTagProtocol(); //Set protocol to GEN2
  nano.setAntennaPort(); //Set TX/RX antenna ports to 1

  return (true); //We are ready to rock
}

void lowBeep()
{
  tone(BUZZER_PIN, 130, 150); //Low C
  //delay(150);
}

void highBeep()
{
  tone(BUZZER_PIN, 2093, 150); //High C
  //delay(150);
}

void printWiFiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(String(rssi));
  Serial.println(" dBm");
}
