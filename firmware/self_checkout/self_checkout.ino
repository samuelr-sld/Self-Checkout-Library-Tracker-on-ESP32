/*
  Library Self Check-out System
  Multi-page GUI with touch navigation, RFID detection, and USB barcode scanner
  Now with Firebase Firestore integration!
  
  Pages:
  1. Home Screen - Idle, waiting for touch input
  2. Action Selection - Choose between Borrow or Return
  3. RFID Input Screen - Detects RFID cards and displays UID (touch to proceed)
  4. Book Input Screen - Reads barcode from USB scanner and displays it (touch to proceed)
  5. Confirmation Screen - Shows scanned RFID and barcode (touch to return home)
  
  Hardware:
  - TFT Touch Screen (240x320)
  - MFRC522 RFID Reader (SPI)
  - USB Barcode Scanner (connected to laptop, reads from Serial/USB)
  
  Firebase Setup Required:
  1. Install libraries: WiFi, Firebase ESP32 Client
  2. Copy secrets.example.h to secrets.h and set your WiFi credentials
  3. Get Firebase credentials from Firebase Console
  4. Update Firebase configuration below
  
  Barcode Scanner Setup:
  - Barcode scanner connected to laptop via USB
  - ESP32 connected to laptop via USB
  - Barcode scanner data is read from Serial port (USB connection)
  - Make sure barcode scanner is configured for Serial/RS-232 mode
  - Serial Monitor can be used to view/debug barcode input
*/

#include "FS.h"
#include <SPI.h>
#include <TFT_eSPI.h>      // Hardware-specific
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include "secrets.h"       // WIFI_SSID, WIFI_PASSWORD (not committed)

TFT_eSPI tft = TFT_eSPI(); // Invoke custom library

// RFID Setup
constexpr uint8_t RST_PIN = 22;
constexpr uint8_t SS_PIN = 21;
MFRC522 mfrc522(SS_PIN, RST_PIN);

// Touch calibration file
#define CALIBRATION_FILE "/TouchCalData1"
#define REPEAT_CAL false

// Page enumeration
enum Page {
  PAGE_HOME = 0,
  PAGE_ACTION_SELECT = 1,
  PAGE_RFID_INPUT = 2,
  PAGE_BOOK_INPUT = 3,
  PAGE_CONFIRMATION = 4
};

Page currentPage = PAGE_HOME;

// Touch debouncing
unsigned long lastTouchTime = 0;
const unsigned long TOUCH_DEBOUNCE = 300; // milliseconds

// Data storage
String rfidUID = "";
String bookBarcode = "";
String userName = "";
String studentId = "";
String bookName = "";
String bookAuthor = "";
String actionType = "borrow";  // "borrow" or "return"
bool cardRegistered = false;
bool checkingCardRegistration = false;
bool waitingForBookInfo = false;
String syncSessionId = "";  // Unique session ID for real-time sync
unsigned long lastUnregisteredMessage = 0;
const unsigned long UNREGISTERED_MESSAGE_INTERVAL = 2000; // 2 seconds

// RFID detection debouncing
unsigned long lastRFIDCheck = 0;
const unsigned long RFID_CHECK_INTERVAL = 200; // milliseconds
bool rfidDetected = false;

// Barcode will come from website via Firestore sync channel

// WiFi Configuration
// Credentials live in secrets.h (gitignored) - copy secrets.example.h to create it
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// Firebase Firestore Configuration (matches your web config from firebase-config.js)
const char* FIREBASE_PROJECT_ID = "self-checkout-library-system";
const char* FIREBASE_API_KEY = "AIzaSyCG_dtaIXO9TVNZJ45f3G7u_3KEKHMAoxc";
const char* FIRESTORE_URL = "https://firestore.googleapis.com/v1/projects/self-checkout-library-system/databases/(default)/documents";

// NOTE: Firestore REST API requires proper authentication
// For production, you may need:
// 1. Service Account authentication (more secure)
// 2. OAuth2 token
// 3. Or set Firestore rules to allow public writes (NOT recommended for production)
// For testing: Update Firestore security rules to allow writes temporarily

// WiFi and Firebase connection status
bool wifiConnected = false;
bool firebaseReady = false;
bool sendingToFirebase = false;

// Forward function declarations
void checkCardRegistration(String normalizedRFID);
void checkForBarcodeFromWebsite();
void checkForBookInfo();
void cleanupSyncDocument(String sessionId);
void markBarcodeReceived();
void sendRFIDForRegistration(String rfidCardId);
void updateBookStatus(String barcode, String action);
void sendCheckoutToFirebase();
String getFirestoreTimestamp();
String getCheckoutTimestampRFC3339();

// Real-time sync timing
unsigned long lastSyncCheck = 0;
const unsigned long SYNC_CHECK_INTERVAL = 1000; // Check every second

//------------------------------------------------------------------------------------------

void setup() {
  // Initialize serial (USB connection to laptop)
  // Barcode scanner data will come through this Serial port
  Serial.begin(115200);  // Higher baud rate for debugging
  delay(100);
  
  Serial.println("System Initializing...");
  
  // Initialize SPI for both TFT and RFID
  SPI.begin();
  
  // Initialize RFID reader
  mfrc522.PCD_Init();
  Serial.println("RFID Reader Ready");
  
  // Initialize TFT screen
  tft.init();
  tft.setRotation(0);
  
  // Calibrate touch screen
  touch_calibrate();
  
  // Connect to WiFi
  connectWiFi();
  
  // Initialize Firebase (only if WiFi connected)
  if (wifiConnected) {
    initFirebase();
  }
  
  // Clear screen and show home page
  tft.fillScreen(TFT_BLACK);
  showHomePage();
  
  Serial.println("System Ready");
}

//------------------------------------------------------------------------------------------

void loop() {
  uint16_t t_x = 0, t_y = 0;
  bool pressed = tft.getTouch(&t_x, &t_y);
  
  // Check for touch input with debouncing
  if (pressed && (millis() - lastTouchTime > TOUCH_DEBOUNCE)) {
    lastTouchTime = millis();
    handleTouch(t_x, t_y);
  }
  
  // PASSIVE RFID Detection - Always active, regardless of current page
  if (millis() - lastRFIDCheck > RFID_CHECK_INTERVAL) {
  lastRFIDCheck = millis();
  
  // Look for new cards
  if (!mfrc522.PICC_IsNewCardPresent()) {
    // No card present - reset detection flag
    if (rfidDetected && currentPage == PAGE_RFID_INPUT) {
      // Card was removed, allow new detection
      rfidDetected = false;
    }
    return; // Exit early if no card
  }
  
  // Select one of the cards (read card serial)
  if (!mfrc522.PICC_ReadCardSerial()) {
    return; // Exit if can't read serial
  }
  
  // Build RFID UID string (with spaces for display)
  String newUID = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) newUID += "0";
    newUID += String(mfrc522.uid.uidByte[i], HEX);
    if (i < mfrc522.uid.size - 1) newUID += " ";
  }
  newUID.toUpperCase();
  
  // Create normalized version (no spaces) for comparison and database
  String normalizedUID = newUID;
  normalizedUID.replace(" ", "");
  
  // Only process if this is a new card OR we're on RFID page and haven't detected yet
  if (newUID != rfidUID || (currentPage == PAGE_RFID_INPUT && !rfidDetected)) {
    String previousUID = rfidUID;
    rfidUID = newUID;  // Keep original with spaces for display
    
    Serial.print("RFID Card Detected (Passive): ");
    Serial.print(rfidUID);
    Serial.print(" (Normalized: ");
    Serial.print(normalizedUID);
    Serial.println(")");
    
    // Always send RFID to sync channel for website registration (use normalized, no spaces)
    sendRFIDForRegistration(normalizedUID);
    
    // If on RFID Input page, handle checkout flow
    if (currentPage == PAGE_RFID_INPUT && !rfidDetected) {
      cardRegistered = false;
      userName = "";
      studentId = "";
      
      // Check if card is registered in Firestore (use normalized UID)
      checkCardRegistration(normalizedUID);
      
      // Update display with RFID UID
      updateRFIDDisplay();
      rfidDetected = true;
    }
  }
  
  // Important: Halt the card and stop crypto to allow re-detection
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}
  
  // ESP32 waits for barcode from website (barcode scanner is connected to laptop via USB)
  // Barcode will come through Firestore sync channel
  
  // Only check for barcode when on BOOK_INPUT page (similar to RFID but reversed - website sends to ESP32)
  if (wifiConnected && firebaseReady && currentPage == PAGE_BOOK_INPUT) {
    if (millis() - lastSyncCheck > SYNC_CHECK_INTERVAL) {
      lastSyncCheck = millis();
      
      // Check for barcode from website
      if (bookBarcode.length() == 0) {
        // Only check if we don't already have a barcode
        checkForBarcodeFromWebsite();
      }
      
      // Check for book info - look up barcode directly in barcodes collection
      if (bookBarcode.length() > 0 && waitingForBookInfo) {
        checkForBookInfo();
      }
    }
  }
}

//------------------------------------------------------------------------------------------

void handleTouch(uint16_t t_x, uint16_t t_y) {
  // Navigate to next page based on current page
  switch (currentPage) {
    case PAGE_HOME:
      showActionSelectPage();
      break;
      
    case PAGE_ACTION_SELECT:
      // Check which button was pressed
      if (t_y >= 120 && t_y <= 180 && t_x >= 20 && t_x <= 220) {
        // Borrow button pressed
        actionType = "borrow";
        showRFIDInputPage();
      } else if (t_y >= 200 && t_y <= 260 && t_x >= 20 && t_x <= 220) {
        // Return button pressed
        actionType = "return";
        showRFIDInputPage();
      }
      // If neither button, do nothing
      break;
      
    case PAGE_RFID_INPUT:
      // Only proceed if card is registered
      if (cardRegistered && userName.length() > 0) {
        showBookInputPage();
      } else if (rfidUID.length() > 0 && !cardRegistered && !checkingCardRegistration) {
        // Card detected but not registered - clear and retry
        Serial.println("Unregistered card - clearing for retry");
        rfidUID = "";
        rfidDetected = false;
        userName = "";
        studentId = "";
        cardRegistered = false;
        lastUnregisteredMessage = 0;
        updateRFIDDisplay(); // Show "Card Unknown" message
      } else {
        // No card detected yet or still checking - do nothing
        Serial.println("No card detected or still checking registration");
      }
      break;
      
    case PAGE_BOOK_INPUT:
      showConfirmationPage();
      break;
      
    case PAGE_CONFIRMATION:
      showHomePage();
      break;
  }
}

//------------------------------------------------------------------------------------------

void showHomePage() {
  currentPage = PAGE_HOME;
  // Reset all values for new checkout session
  rfidUID = "";
  bookBarcode = "";
  rfidDetected = false;  // ✓ Already present
  userName = "";
  studentId = "";
  bookName = "";
  bookAuthor = "";
  actionType = "borrow";  // Reset to default
  cardRegistered = false;
  checkingCardRegistration = false;  // Add this line
  waitingForBookInfo = false;
  syncSessionId = "";
  lastUnregisteredMessage = 0;  // Add this line to reset the message timer
  
  tft.fillScreen(TFT_WHITE);
  
  // Title
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(0x0019, TFT_WHITE);  // Navy blue
  tft.setFreeFont(&FreeSansBold18pt7b);  // Smaller font
  tft.drawString("Library", 120, 40);
  tft.drawString("Check-out", 120, 65);
  tft.drawString("System", 120, 90);
  
  // Instructions
  tft.setTextColor(0x0019, TFT_WHITE);  // Navy blue
  tft.setFreeFont(&FreeSans9pt7b);  // Smaller font
  tft.drawString("Touch screen to begin", 120, 200);
  
  // Decorative elements
  tft.drawRect(10, 10, 220, 140, 0xFDA0);  // Gold accents
  tft.drawRect(12, 12, 216, 136, 0xFDA0);
}

//------------------------------------------------------------------------------------------

void showActionSelectPage() {
  currentPage = PAGE_ACTION_SELECT;
  
  tft.fillScreen(TFT_WHITE);
  
  // Title
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(0x0019, TFT_WHITE);  // Navy blue
  tft.setFreeFont(&FreeSansBold12pt7b);  // Smaller font
  tft.drawString("Select Action", 120, 40);
  
  // Instructions
  tft.setTextColor(0x0019, TFT_WHITE);  // Navy blue
  tft.setFreeFont(&FreeSans9pt7b);  // Smaller font
  tft.drawString("Choose what you want to do:", 120, 80);
  
  // Borrow button
  tft.fillRect(20, 120, 200, 60, 0x0019);  // Navy blue
  tft.drawRect(20, 120, 200, 60, 0xFDA0);  // Gold border
  tft.setTextColor(TFT_WHITE, 0x0019);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.drawString("BORROW BOOK", 120, 150);
  
  // Return button
  tft.fillRect(20, 200, 200, 60, 0x0019);  // Navy blue
  tft.drawRect(20, 200, 200, 60, 0xFDA0);  // Gold border
  tft.setTextColor(TFT_WHITE, 0x0019);
  tft.drawString("RETURN BOOK", 120, 230);
  
  // Footer instruction
  tft.setTextColor(0x0019, TFT_WHITE);  // Navy blue
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Touch your choice to continue", 120, 290);
}

//------------------------------------------------------------------------------------------

void showRFIDInputPage() {
  currentPage = PAGE_RFID_INPUT;
  rfidDetected = false;
  rfidUID = ""; // Clear previous RFID
  userName = ""; // Clear previous username
  studentId = ""; // Clear previous student ID
  cardRegistered = false;
  checkingCardRegistration = false;
  
  tft.fillScreen(TFT_WHITE);
  
  // Title
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(0xFDA0, TFT_WHITE);  // Gold
  tft.setFreeFont(&FreeSansBold12pt7b);  // Smaller
  String titleText = (actionType == "return") ? "Return - User ID" : "Borrow - User ID";
  tft.drawString(titleText, 120, 40);
  
  // Instructions
  tft.setTextColor(0x0019, TFT_WHITE);  // Navy blue
  tft.setFreeFont(&FreeSans9pt7b);  // Smaller
  tft.drawString("Please scan your", 120, 100);
  tft.drawString("registered card", 120, 120);
  
  // Status area - larger for displaying UID
  tft.fillRect(10, 140, 220, 90, 0xEEEE);  // Light gray
  tft.drawRect(10, 140, 220, 90, 0x0019);  // Navy border
  
  // Display waiting message
  tft.setTextColor(0x0019, 0xEEEE);  // Navy on light gray
  tft.setFreeFont(&FreeSans9pt7b);  // Smaller
  tft.drawString("Waiting for card...", 120, 175);
  
  tft.setTextColor(0x0019, 0xEEEE);
  tft.setFreeFont(&FreeSans9pt7b);  // Even smaller
  tft.drawString("Scan your RFID", 120, 195);
  
  // Navigation hint - removed for cleaner look
  tft.setTextColor(0x0019, TFT_WHITE);  // Navy
  tft.setFreeFont(&FreeSans9pt7b);  // Small
  tft.drawString("Registered cards only", 120, 280);
}


//------------------------------------------------------------------------------------------

void updateRFIDDisplay() {
  // Clear the display area - make it taller for user info
  tft.fillRect(10, 140, 220, 90, 0xEEEE);  // Light gray
  tft.drawRect(10, 140, 220, 90, 0x0019);  // Navy border
  
  if (rfidUID.length() > 0) {
    tft.setTextDatum(TC_DATUM);
    
    if (checkingCardRegistration) {
      // Still checking with database
      tft.setTextColor(0xFDA0, 0xEEEE);  // Gold on light gray
      tft.setFreeFont(&FreeSansBold9pt7b);  // Smaller
      tft.drawString("Checking...", 120, 175);
      
      tft.setTextColor(0x0019, 0xEEEE);  // Navy on light gray
      tft.setFreeFont(&FreeSans9pt7b);  // Smaller
      tft.drawString("Please wait", 120, 195);
      
    } else if (cardRegistered && userName.length() > 0) {
      // Card is registered - show welcome message with username
      tft.setTextColor(0xFDA0, 0xEEEE);  // Gold
      tft.setFreeFont(&FreeSansBold9pt7b);  // Smaller
      tft.drawString("Welcome!", 120, 150);
      
      // Display username (main identifier)
      tft.setTextColor(0x0019, 0xEEEE);  // Navy
      tft.setFreeFont(&FreeSansBold9pt7b);  // Smaller
      String displayName = userName;
      if (displayName.length() > 18) {
        displayName = displayName.substring(0, 18) + "...";
      }
      tft.drawString(displayName, 120, 175);
      
      // Display student ID if available (secondary info)
      if (studentId.length() > 0) {
        tft.setTextColor(0x0019, 0xEEEE);  // Navy
        tft.setFreeFont(&FreeSans9pt7b);  // Even smaller
        String displayId = studentId;
        if (displayId.length() > 20) {
          displayId = displayId.substring(0, 20) + "...";
        }
        tft.drawString("Email: " + displayId, 120, 195);
      }
      
      // Show instruction to proceed
      tft.setTextColor(0x0019, 0xEEEE);  // Navy
      tft.setFreeFont(&FreeSans9pt7b);  // Smaller
      tft.drawString("Touch to continue", 120, 213);
      
    } else if (!cardRegistered && !checkingCardRegistration) {
      // Card not registered - BLOCK with clear message
      tft.setTextColor(TFT_RED, 0xEEEE);  // Red error
      tft.setFreeFont(&FreeSansBold9pt7b);  // Smaller
      tft.drawString("Card Unknown", 120, 160);
      
      tft.setTextColor(0xFDA0, 0xEEEE);  // Gold warning
      tft.setFreeFont(&FreeSansBold9pt7b);  // Smaller
      tft.drawString("Please Try Again", 120, 185);
      
      tft.setTextColor(0x0019, 0xEEEE);  // Navy
      tft.setFreeFont(&FreeSans9pt7b);  // Smaller
      tft.drawString("Remove & rescan card", 120, 210);
      
    } else {
      // Card detected but info not loaded yet
      tft.setTextColor(0x0019, 0xEEEE);  // Navy
      tft.setFreeFont(&FreeSansBold9pt7b);  // Smaller
      tft.drawString("Card Detected", 120, 175);
      
      tft.setTextColor(0x0019, 0xEEEE);  // Navy
      tft.setFreeFont(&FreeSans9pt7b);  // Smaller
      tft.drawString("Loading...", 120, 195);
    }
  } else {
    // No card detected - show waiting message
    tft.setTextColor(0x0019, 0xEEEE);  // Navy on light gray
    tft.setFreeFont(&FreeSans9pt7b);  // Smaller
    tft.drawString("Waiting for card...", 120, 175);
    
    tft.setTextColor(0x0019, 0xEEEE);  // Navy on light gray
    tft.setFreeFont(&FreeSans9pt7b);  // Smaller
    tft.drawString("Scan your RFID", 120, 195);
  }
}

//------------------------------------------------------------------------------------------

void showBookInputPage() {
  currentPage = PAGE_BOOK_INPUT;
  bookBarcode = ""; // Clear previous barcode
  bookName = ""; // Clear previous book info
  bookAuthor = ""; // Clear previous author
  waitingForBookInfo = false;
  syncSessionId = ""; // Clear sync session
  tft.fillScreen(TFT_WHITE);
  
  // Title
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(0xFDA0, TFT_WHITE);  // Gold
  tft.setFreeFont(&FreeSansBold12pt7b);  // Smaller
  String titleText = (actionType == "return") ? "Return Book" : "Borrow Book";
  tft.drawString(titleText, 120, 40);
  
  // Instructions
  tft.setTextColor(0x0019, TFT_WHITE);  // Navy blue
  tft.setFreeFont(&FreeSans9pt7b);  // Smaller
  tft.drawString("Scan barcode on", 120, 100);
  tft.drawString("website/computer", 120, 120);
  
  // Input area - larger for displaying barcode
  tft.fillRect(10, 160, 220, 60, 0xEEEE);  // Light gray
  tft.drawRect(10, 160, 220, 60, 0x0019);  // Navy border
  
  // Display waiting message or barcode
  if (bookBarcode.length() == 0) {
    tft.setTextColor(0xFDA0, 0xEEEE);  // Gold on light gray
    tft.setFreeFont(&FreeSans9pt7b);  // Smaller
    tft.drawString("Waiting for", 120, 175);
    tft.drawString("barcode scan...", 120, 195);
  } else {
    updateBookInputDisplay();
  }
  
  // Navigation hint
  tft.setTextColor(0x0019, TFT_WHITE);  // Navy
  tft.setFreeFont(&FreeSans9pt7b);  // Smaller
  tft.drawString("Touch to continue", 120, 280);
}

//------------------------------------------------------------------------------------------

void updateBookInputDisplay() {
  // Clear the display area - make taller for book info
  tft.fillRect(10, 140, 220, 100, 0xEEEE);  // Light gray
  tft.drawRect(10, 140, 220, 100, 0x0019);  // Navy border
  
  if (bookBarcode.length() > 0) {
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(0xFDA0, 0xEEEE);  // Gold on light gray
    tft.setFreeFont(&FreeSansBold9pt7b);  // Smaller
    tft.drawString("Barcode Scanned!", 120, 150);
    
    // Display barcode
    tft.setTextColor(0x0019, 0xEEEE);  // Navy on light gray
    tft.setFreeFont(&FreeSans9pt7b);  // Smaller
    String displayBarcode = bookBarcode;
    if (displayBarcode.length() > 20) {
      displayBarcode = displayBarcode.substring(0, 20) + "...";
    }
    tft.drawString(displayBarcode, 120, 170);
    
    // Display book info if received
    if (waitingForBookInfo) {
      tft.setTextColor(0x0019, 0xEEEE);  // Navy on light gray
      tft.setFreeFont(&FreeSans9pt7b);  // Smaller
      tft.drawString("Looking up book...", 120, 195);
    } else if (bookName.length() > 0) {
      tft.setTextColor(0x0019, 0xEEEE);  // Navy on light gray
      tft.setFreeFont(&FreeSans9pt7b);  // Smaller
      String displayTitle = bookName;
      if (displayTitle.length() > 18) {
        displayTitle = displayTitle.substring(0, 18) + "...";
      }
      tft.drawString(displayTitle, 120, 195);
      
      if (bookAuthor.length() > 0) {
        tft.setTextColor(0x0019, 0xEEEE);  // Navy on light gray
        tft.setFreeFont(&FreeSans9pt7b);  // Even smaller
        String displayAuthor = bookAuthor;
        if (displayAuthor.length() > 18) {
          displayAuthor = displayAuthor.substring(0, 18) + "...";
        }
        tft.drawString(displayAuthor, 120, 210);
      }
    }
  } else {
    tft.setTextColor(0x0019, 0xEEEE);  // Navy on light gray
    tft.setFreeFont(&FreeSans9pt7b);  // Smaller
    tft.drawString("Waiting for scan...", 120, 185);
  }
}

//------------------------------------------------------------------------------------------

void showConfirmationPage() {
  currentPage = PAGE_CONFIRMATION;
  tft.fillScreen(TFT_WHITE);
  
  // Title
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(0xFDA0, TFT_WHITE);  // Gold
  tft.setFreeFont(&FreeSansBold12pt7b);  // Smaller
  tft.drawString("Confirmation", 120, 40);
  
  // Confirmation message
  tft.setTextColor(0x0019, TFT_WHITE);  // Navy blue
  tft.setFreeFont(&FreeSans9pt7b);  // Smaller
  String actionMessage = (actionType == "return") ? "Return complete!" : "Check-out complete!";
  tft.drawString(actionMessage, 120, 90);
  
  // Details area
  tft.fillRect(10, 120, 220, 90, 0xEEEE);  // Light gray
  tft.drawRect(10, 120, 220, 90, 0x0019);  // Navy border
  
  // Display User Info - SHOW USERNAME instead of RFID
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(0x0019, 0xEEEE);  // Navy on light gray
  tft.setFreeFont(&FreeSans9pt7b);  // Smaller
  tft.drawString("User:", 20, 135);
  
  tft.setTextColor(0xFDA0, 0xEEEE);  // Gold on light gray
  if (userName.length() > 0) {
    // Show username as primary identifier
    tft.setFreeFont(&FreeSansBold9pt7b);  // Smaller
    String displayUser = userName;
    if (displayUser.length() > 23) {
      displayUser = displayUser.substring(0, 23) + "...";
    }
    tft.drawString(displayUser, 20, 153);
  } else {
    // Fallback to RFID if username not available (shouldn't happen)
    tft.setFreeFont(&FreeSans9pt7b);  // Smaller
    String displayUID = rfidUID;
    if (displayUID.length() > 23) {
      displayUID = displayUID.substring(0, 23) + "...";
    }
    tft.drawString(displayUID, 20, 153);
  }
  
  // Display Book Info
  tft.setTextColor(0x0019, 0xEEEE);  // Navy on light gray
  tft.setFreeFont(&FreeSans9pt7b);  // Smaller
  tft.drawString("Book:", 20, 178);
  
  tft.setTextColor(0xFDA0, 0xEEEE);  // Gold on light gray
  if (bookName.length() > 0) {
    tft.setFreeFont(&FreeSansBold9pt7b);  // Smaller
    String displayBook = bookName;
    if (displayBook.length() > 23) {
      displayBook = displayBook.substring(0, 23) + "...";
    }
    tft.drawString(displayBook, 20, 196);
  } else if (bookBarcode.length() > 0) {
    tft.setFreeFont(&FreeSans9pt7b);  // Smaller
    String displayBarcode = bookBarcode;
    if (displayBarcode.length() > 23) {
      displayBarcode = displayBarcode.substring(0, 23) + "...";
    }
    tft.drawString(displayBarcode, 20, 196);
  } else {
    tft.setFreeFont(&FreeSans9pt7b);  // Smaller
    tft.drawString("Not scanned", 20, 196);
  }
  
  // Firebase status
  tft.setTextDatum(TC_DATUM);
  if (firebaseReady && rfidUID.length() > 0 && bookBarcode.length() > 0) {
    tft.setTextColor(0x0019, TFT_WHITE);  // Navy blue
    tft.setFreeFont(&FreeSans9pt7b);  // Smaller
    tft.drawString("Saving to cloud...", 120, 235);
    if (!sendingToFirebase) {
      sendingToFirebase = true;
      // Clean up the barcode sync document BEFORE saving
      // so the next session doesn't re-read the old barcode
      cleanupSyncDocument(syncSessionId);
      sendCheckoutToFirebase();
    }
  } else if (!wifiConnected) {
    tft.setTextColor(TFT_RED, TFT_WHITE);  // Red on white
    tft.setFreeFont(&FreeSans9pt7b);  // Smaller
    tft.drawString("WiFi not connected", 120, 235);
  }
  
  // Return instruction
  tft.setTextColor(0xFDA0, TFT_WHITE);  // Gold
  tft.setFreeFont(&FreeSansBold9pt7b);  // Smaller
  tft.drawString("Touch to return", 120, 280);
  tft.drawString("to home", 120, 300);
}

//------------------------------------------------------------------------------------------

void touch_calibrate()
{
  uint16_t calData[5];
  uint8_t calDataOK = 0;

  // Check file system exists
  if (!SPIFFS.begin()) {
    Serial.println("formatting file system");
    SPIFFS.format();
    SPIFFS.begin();
  }

  // Check if calibration file exists and size is correct
  if (SPIFFS.exists(CALIBRATION_FILE)) {
    if (REPEAT_CAL)
    {
      // Delete if we want to re-calibrate
      SPIFFS.remove(CALIBRATION_FILE);
    }
    else
    {
      File f = SPIFFS.open(CALIBRATION_FILE, "r");
      if (f) {
        if (f.readBytes((char *)calData, 14) == 14)
          calDataOK = 1;
        f.close();
      }
    }
  }

  if (calDataOK && !REPEAT_CAL) {
    // Calibration data valid
    tft.setTouch(calData);
  } else {
    // Data not valid so recalibrate
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(20, 0);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    tft.println("Touch corners as indicated");

    tft.setTextFont(1);
    tft.println();

    if (REPEAT_CAL) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.println("Set REPEAT_CAL to false to stop this running again!");
    }

    tft.calibrateTouch(calData, TFT_MAGENTA, TFT_BLACK, 15);

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.println("Calibration complete!");

    // Store data
    File f = SPIFFS.open(CALIBRATION_FILE, "w");
    if (f) {
      f.write((const unsigned char *)calData, 14);
      f.close();
    }
  }
}

//------------------------------------------------------------------------------------------

void connectWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("");
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Configure NTP to get accurate timestamps
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    Serial.println("Waiting for NTP time sync...");
    int retry = 0;
    time_t now = time(nullptr);
    while (now < 1600000000 && retry < 15) { // wait until year ~2020
      delay(1000);
      Serial.print(".");
      now = time(nullptr);
      retry++;
    }
    Serial.println("");
    if (now >= 1600000000) {
      Serial.println("NTP time synced: ");
      struct tm timeinfo;
      gmtime_r(&now, &timeinfo);
      char buf[32];
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &timeinfo);
      Serial.println(buf);
    } else {
      Serial.println("NTP time not synced; timestamps may be inaccurate");
    }
  } else {
    wifiConnected = false;
    Serial.println("");
    Serial.println("WiFi connection failed!");
  }
}


//------------------------------------------------------------------------------------------

void initFirebase() {
  Serial.println("Initializing Firebase Firestore...");
  
  // Test connection by making a simple request
  // For Firestore REST API, we'll use HTTPClient
  // Just mark as ready if WiFi is connected
  if (wifiConnected) {
    firebaseReady = true;
    Serial.println("Firebase Firestore ready!");
    Serial.print("Project ID: ");
    Serial.println(FIREBASE_PROJECT_ID);
  } else {
    firebaseReady = false;
    Serial.println("Firebase not ready - WiFi not connected");
  }
}

//------------------------------------------------------------------------------------------

void checkCardRegistration(String normalizedRFID) {
  if (!firebaseReady || normalizedRFID.length() == 0) {
    return;
  }
  
  checkingCardRegistration = true;
  Serial.println("=== Checking card registration ===");
  Serial.print("Normalized RFID: ");
  Serial.println(normalizedRFID);
  
  String url = String(FIRESTORE_URL) + "/cards?key=" + String(FIREBASE_API_KEY);
  
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(url);
  
  int httpResponseCode = http.GET();
  
  Serial.print("HTTP Response Code: ");
  Serial.println(httpResponseCode);
  
  if (httpResponseCode == 200) {
    String response = http.getString();
    String search = normalizedRFID;
    search.toUpperCase();
    
    Serial.print("Searching for normalized RFID: ");
    Serial.println(search);
    
    int docsPos = response.indexOf("\"documents\"");
    if (docsPos == -1) {
      Serial.println("No documents found");
      cardRegistered = false;
      http.end();
      checkingCardRegistration = false;
      updateRFIDDisplay();
      return;
    }
    
    Serial.println("Found 'documents' array in response");
    
    bool found = false;
    int docStart = response.indexOf("{", docsPos);
    int checked = 0;
    
    while (docStart != -1 && !found && checked < 50) {
      checked++;
      
      int docEnd = docStart + 1;
      int braces = 1;
      while (docEnd < response.length() && braces > 0) {
        if (response.charAt(docEnd) == '{') braces++;
        if (response.charAt(docEnd) == '}') braces--;
        docEnd++;
      }
      
      if (braces == 0) {
        String doc = response.substring(docStart, docEnd);
        unsigned int docLen = doc.length();
        
        Serial.print("Document ");
        Serial.print(checked);
        Serial.println(" extracted");
        
        // Look for "cardId" field FIRST
        int cardIdFieldPos = doc.indexOf("\"cardId\"");
        if (cardIdFieldPos != -1) {
          // Make sure this is in the "fields" section, not the document metadata
          int fieldsPos = doc.indexOf("\"fields\"");
          if (fieldsPos == -1 || cardIdFieldPos < fieldsPos) {
            // cardId is before fields section, skip this
            Serial.println("'cardId' found but not in fields section, skipping");
            docStart = response.indexOf("{", docEnd);
            continue;
          }
          
          Serial.println("Looking for 'cardId' field in fields section... FOUND!");
          
          unsigned int sectionLimit = (unsigned int)cardIdFieldPos + 300;
          if (sectionLimit > docLen) sectionLimit = docLen;
          String section = doc.substring(cardIdFieldPos, sectionLimit);
          
          int stringValuePos = section.indexOf("\"stringValue\"");
          
          if (stringValuePos != -1) {
            int colonPos = section.indexOf(":", stringValuePos);
            if (colonPos != -1) {
              int quotePos = section.indexOf("\"", colonPos);
              if (quotePos != -1) {
                int valueStart = quotePos + 1;
                int valueEnd = section.indexOf("\"", valueStart);
                
                if (valueEnd > valueStart) {
                  String dbCardId = section.substring(valueStart, valueEnd);
                  String normalizedDbCardId = dbCardId;
                  normalizedDbCardId.replace(" ", "");
                  normalizedDbCardId.toUpperCase();
                  
                  Serial.print("Found cardId in DB: '");
                  Serial.print(normalizedDbCardId);
                  Serial.println("'");
                  
                  if (normalizedDbCardId == search) {
                    found = true;
                    cardRegistered = true;
                    Serial.println("*** CARD MATCH! ***");
                    
                    // Now extract NAME field - look AFTER fieldsPos to avoid metadata
                    int nameSearchStart = fieldsPos;
                    int namePos = doc.indexOf("\"name\"", nameSearchStart);
                    
                    // Make sure we're finding the name in fields, not document name
                    while (namePos != -1) {
                      // Check if this "name" is followed by a ":" and then "{"
                      // Pattern should be: "name": { "stringValue": "Samuel" }
                      int checkColon = doc.indexOf(":", namePos);
                      if (checkColon != -1 && checkColon < namePos + 10) {
                        int checkBrace = doc.indexOf("{", checkColon);
                        int checkQuote = doc.indexOf("\"", checkColon);
                        
                        // If we find { before ", this is a field, not metadata
                        if (checkBrace != -1 && checkBrace < checkQuote) {
                          // This is the actual name field!
                          unsigned int nameLimit = (unsigned int)namePos + 300;
                          if (nameLimit > docLen) nameLimit = docLen;
                          String nameSec = doc.substring(namePos, nameLimit);
                          
                          int nameSv = nameSec.indexOf("\"stringValue\"");
                          if (nameSv != -1) {
                            int nameColon = nameSec.indexOf(":", nameSv);
                            int nameQuote = nameSec.indexOf("\"", nameColon);
                            if (nameQuote != -1) {
                              int nameStart = nameQuote + 1;
                              int nameEnd = nameSec.indexOf("\"", nameStart);
                              if (nameEnd > nameStart) {
                                userName = nameSec.substring(nameStart, nameEnd);
                                Serial.print("User NAME: ");
                                Serial.println(userName);
                                break; // Found name, stop searching
                              }
                            }
                          }
                        }
                      }
                      
                      // Continue searching for next "name"
                      namePos = doc.indexOf("\"name\"", namePos + 6);
                    }
                    
                    // Extract EMAIL field
                    int emailPos = doc.indexOf("\"email\"", fieldsPos);
                    if (emailPos != -1) {
                      unsigned int emailLimit = (unsigned int)emailPos + 300;
                      if (emailLimit > docLen) emailLimit = docLen;
                      String emailSec = doc.substring(emailPos, emailLimit);
                      
                      int emailSv = emailSec.indexOf("\"stringValue\"");
                      if (emailSv != -1) {
                        int emailColon = emailSec.indexOf(":", emailSv);
                        int emailQuote = emailSec.indexOf("\"", emailColon);
                        if (emailQuote != -1) {
                          int emailStart = emailQuote + 1;
                          int emailEnd = emailSec.indexOf("\"", emailStart);
                          if (emailEnd > emailStart) {
                            studentId = emailSec.substring(emailStart, emailEnd);
                            Serial.print("User EMAIL: ");
                            Serial.println(studentId);
                          }
                        }
                      }
                    }
                    
                    break; // Found our card, stop checking documents
                  }
                }
              }
            }
          }
        } else {
          Serial.println("'cardId' field not found in this document");
        }
      }
      
      docStart = response.indexOf("{", docEnd);
    }
    
    Serial.print("Checked ");
    Serial.print(checked);
    Serial.println(" documents");
    
    if (!found) {
      cardRegistered = false;
      Serial.println("Card NOT found in database");
    }
  } else {
    Serial.print("HTTP error: ");
    Serial.println(httpResponseCode);
    cardRegistered = false;
  }
  
  http.end();
  checkingCardRegistration = false;
  Serial.print("=== End card check - cardRegistered: ");
  Serial.println(cardRegistered ? "TRUE" : "FALSE");
  
  updateRFIDDisplay();
  
  Serial.print("Display state - cardRegistered: ");
  Serial.print(cardRegistered);
  Serial.print(", userName: ");
  Serial.print(userName);
  Serial.print(", email (stored in studentId): ");
  Serial.println(studentId);
}
                    
                    // Now extract NAME field - look AFTER fieldsPos to avoid metadata
                    int nameSearchStart = fieldsPos;
                    int namePos = doc.indexOf("\"name\"", nameSearchStart);
                    
                    // Make sure we're finding the name in fields, not document name
                    while (namePos != -1) {
                      // Check if this "name" is followed by a ":" and then "{"
                      // Pattern should be: "name": { "stringValue": "Samuel" }
                      int checkColon = doc.indexOf(":", namePos);
                      if (checkColon != -1 && checkColon < namePos + 10) {
                        int checkBrace = doc.indexOf("{", checkColon);
                        int checkQuote = doc.indexOf("\"", checkColon);
                        
                        // If we find { before ", this is a field, not metadata
                        if (checkBrace != -1 && checkBrace < checkQuote) {
                          // This is the actual name field!
                          unsigned int nameLimit = (unsigned int)namePos + 300;
                          if (nameLimit > docLen) nameLimit = docLen;
                          String nameSec = doc.substring(namePos, nameLimit);
                          
                          int nameSv = nameSec.indexOf("\"stringValue\"");
                          if (nameSv != -1) {
                            int nameColon = nameSec.indexOf(":", nameSv);
                            int nameQuote = nameSec.indexOf("\"", nameColon);
                            if (nameQuote != -1) {
                              int nameStart = nameQuote + 1;
                              int nameEnd = nameSec.indexOf("\"", nameStart);
                              if (nameEnd > nameStart) {
                                userName = nameSec.substring(nameStart, nameEnd);
                                Serial.print("User NAME: ");
                                Serial.println(userName);
                                break; // Found name, stop searching
                              }
                            }
                          }
                        }
                      }
                      
                      // Continue searching for next "name"
                      namePos = doc.indexOf("\"name\"", namePos + 6);
                    }
                    
                    // Extract EMAIL field
                    int emailPos = doc.indexOf("\"email\"", fieldsPos);
                    if (emailPos != -1) {
                      unsigned int emailLimit = (unsigned int)emailPos + 300;
                      if (emailLimit > docLen) emailLimit = docLen;
                      String emailSec = doc.substring(emailPos, emailLimit);
                      
                      int emailSv = emailSec.indexOf("\"stringValue\"");
                      if (emailSv != -1) {
                        int emailColon = emailSec.indexOf(":", emailSv);
                        int emailQuote = emailSec.indexOf("\"", emailColon);
                        if (emailQuote != -1) {
                          int emailStart = emailQuote + 1;
                          int emailEnd = emailSec.indexOf("\"", emailStart);
                          if (emailEnd > emailStart) {
                            studentId = emailSec.substring(emailStart, emailEnd);
                            Serial.print("User EMAIL: ");
                            Serial.println(studentId);
                          }
                        }
                      }
                    }
                    
                    break; // Found our card, stop checking documents
                  }
                }
              }
            }
          }
        } else {
          Serial.println("'cardId' field not found in this document");
        }
      }
      
      docStart = response.indexOf("{", docEnd);
    }
    
    Serial.print("Checked ");
    Serial.print(checked);
    Serial.println(" documents");
    
    if (!found) {
      cardRegistered = false;
      Serial.println("Card NOT found in database");
    }
  } else {
    Serial.print("HTTP error: ");
    Serial.println(httpResponseCode);
    cardRegistered = false;
  }
  
  http.end();
  checkingCardRegistration = false;
  Serial.print("=== End card check - cardRegistered: ");
  Serial.println(cardRegistered ? "TRUE" : "FALSE");
  
  updateRFIDDisplay();
  
  Serial.print("Display state - cardRegistered: ");
  Serial.print(cardRegistered);
  Serial.print(", userName: ");
  Serial.print(userName);
  Serial.print(", email (stored in studentId): ");
  Serial.println(studentId);
}
//------------------------------------------------------------------------------------------

void checkForBarcodeFromWebsite() {
  if (!firebaseReady || currentPage != PAGE_BOOK_INPUT) {
    return;
  }
  
  String url = String(FIRESTORE_URL) + "/sync?key=" + String(FIREBASE_API_KEY);
  
  HTTPClient http;
  http.setTimeout(5000);
  http.begin(url);
  
  int httpResponseCode = http.GET();
  
  if (httpResponseCode == 200) {
    String response = http.getString();
    
    int typePos = response.indexOf("\"barcode_from_website\"");
    
    if (typePos != -1) {
      // Extract document name
      int namePos = response.lastIndexOf("\"name\"", typePos);
      int nameStart = response.indexOf("\"", namePos + 6) + 1;
      int nameEnd = response.indexOf("\"", nameStart);
      if (namePos != -1 && nameStart > namePos && nameEnd > nameStart) {
        String docPath = response.substring(nameStart, nameEnd);
        int lastSlash = docPath.lastIndexOf("/");
        if (lastSlash != -1) {
          syncSessionId = docPath.substring(lastSlash + 1);
        }
      }

      // Find the boundaries of this whole document object
      // Search backward from typePos to find the opening { of this document
      int docObjStart = response.lastIndexOf("{\"name\"", typePos);
      if (docObjStart == -1) docObjStart = 0;
      
      // Search forward to find the closing } of this document
      int docObjEnd = typePos;
      int braces = 0;
      for (int i = docObjStart; i < response.length(); i++) {
        if (response.charAt(i) == '{') braces++;
        if (response.charAt(i) == '}') {
          braces--;
          if (braces == 0) {
            docObjEnd = i;
            break;
          }
        }
      }

      // Search for esp32Received within the ENTIRE document block (not just forward)
      String docBlock = response.substring(docObjStart, docObjEnd);
      bool processed = false;
      int espPos = docBlock.indexOf("\"esp32Received\"");
      if (espPos != -1) {
        int boolPos = docBlock.indexOf("booleanValue", espPos);
        if (boolPos != -1 && boolPos < espPos + 60) {
          int truePos = docBlock.indexOf("true", boolPos);
          if (truePos != -1 && truePos < boolPos + 30) {
            processed = true;
            Serial.println("Barcode already processed (esp32Received=true), skipping");
          }
        }
      }
      
      if (!processed && syncSessionId.length() > 0) {
        // Extract barcode value
        int barcodePos = docBlock.indexOf("\"barcode\"");
        while (barcodePos != -1) {
          int svPos = docBlock.indexOf("\"stringValue\"", barcodePos);
          if (svPos == -1 || svPos > barcodePos + 200) break;
          int valStart = docBlock.indexOf("\"", svPos + 14) + 1;
          int valEnd   = docBlock.indexOf("\"", valStart);
          if (valEnd > valStart) {
            String val = docBlock.substring(valStart, valEnd);
            // Skip if value is literally "barcode" (type field) or empty
            if (val != "barcode" && val != "barcode_from_website" && val.length() > 0) {
              bookBarcode = val;
              waitingForBookInfo = true;
              
              Serial.println("*** BARCODE RECEIVED FROM WEBSITE! ***");
              Serial.print("Barcode: "); Serial.println(bookBarcode);
              Serial.print("Session ID: "); Serial.println(syncSessionId);
              
              http.end();
              updateBookInputDisplay();
              markBarcodeReceived();
              return;
            }
          }
          barcodePos = docBlock.indexOf("\"barcode\"", barcodePos + 10);
        }
      }
    }
  }
  
  http.end();
}
//------------------------------------------------------------------------------------------

void markBarcodeReceived() {
  if (syncSessionId.length() == 0) {
    Serial.println("ERROR: Cannot mark barcode received - syncSessionId is empty");
    return;
  }
  
  Serial.println("=== Marking barcode as received by ESP32 ===");
  Serial.print("Document ID: ");
  Serial.println(syncSessionId);
  
  // Update sync document to mark as received by ESP32
  String url = String(FIRESTORE_URL) + "/sync/" + syncSessionId + "?key=" + String(FIREBASE_API_KEY);
  
  Serial.print("URL: ");
  Serial.println(url);
  
  String jsonPayload = "{";
  jsonPayload += "\"fields\": {";
  jsonPayload += "\"esp32Received\": {\"booleanValue\": true}";
  jsonPayload += "}";
  jsonPayload += "}";
  
  Serial.print("Payload: ");
  Serial.println(jsonPayload);
  
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  int httpResponseCode = http.PATCH(jsonPayload);
  
  Serial.print("HTTP Response Code: ");
  Serial.println(httpResponseCode);
  
  if (httpResponseCode == 200) {
    Serial.println("SUCCESS: Barcode marked as received by ESP32");
  } else {
    Serial.print("ERROR: Failed to mark barcode as received. HTTP Code: ");
    Serial.println(httpResponseCode);
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.print("Response: ");
      Serial.println(response);
    }
  }
  
  http.end();
  Serial.println("=== End mark barcode received ===");
}

//------------------------------------------------------------------------------------------

void sendRFIDForRegistration(String rfidCardId) {
  // Check WiFi connection
  if (!wifiConnected) {
    Serial.println("ERROR: WiFi not connected - cannot send RFID");
    return;
  }
  
  // Check Firebase ready
  if (!firebaseReady) {
    Serial.println("ERROR: Firebase not ready - cannot send RFID");
    return;
  }
  
  if (rfidCardId.length() == 0) {
    Serial.println("ERROR: Empty RFID card ID");
    return;
  }
  
  Serial.println("=== Sending RFID to sync channel ===");
  Serial.print("RFID Card ID: ");
  Serial.println(rfidCardId);
  Serial.print("WiFi Status: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
  Serial.print("Firebase Ready: ");
  Serial.println(firebaseReady ? "Yes" : "No");
  
  // Create unique session ID
  String syncDocId = "rfid_reg_" + String(millis());
  
  // Send to sync collection
  String url = String(FIRESTORE_URL) + "/sync?documentId=" + syncDocId + "&key=" + String(FIREBASE_API_KEY);
  
  Serial.print("URL: ");
  Serial.println(url);
  
  // Normalize RFID - remove spaces (database stores without spaces)
  String escapedRFID = rfidCardId;
  escapedRFID.replace(" ", "");  // Remove spaces from RFID to match database format
  escapedRFID.toUpperCase();     // Ensure uppercase
  
  String jsonPayload = "{";
  jsonPayload += "\"fields\": {";
  jsonPayload += "\"rfid\": {\"stringValue\": \"" + escapedRFID + "\"},";
  jsonPayload += "\"cardId\": {\"stringValue\": \"" + escapedRFID + "\"},";
  jsonPayload += "\"type\": {\"stringValue\": \"rfid_for_registration\"},";
  jsonPayload += "\"timestamp\": {\"timestampValue\": \"" + getCheckoutTimestampRFC3339() + "\"},";
  jsonPayload += "\"processed\": {\"booleanValue\": false}";
  jsonPayload += "}";
  jsonPayload += "}";
  
  Serial.print("Payload: ");
  Serial.println(jsonPayload);
  
  HTTPClient http;
  http.setTimeout(10000); // 10 second timeout
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  int httpResponseCode = http.POST(jsonPayload);
  
  Serial.print("HTTP Response Code: ");
  Serial.println(httpResponseCode);
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.print("Response: ");
    Serial.println(response);
    
    if (httpResponseCode == 200 || httpResponseCode == 201) {
      Serial.println("SUCCESS: RFID sent to sync channel for registration");
    } else {
      Serial.print("ERROR: Failed to send RFID. HTTP Code: ");
      Serial.println(httpResponseCode);
    }
  } else {
    Serial.print("ERROR: HTTP request failed. Error code: ");
    Serial.println(httpResponseCode);
    Serial.println("Possible issues:");
    Serial.println("- Check WiFi connection");
    Serial.println("- Check Firebase URL and API key");
    Serial.println("- Check Firestore security rules");
  }
  
  http.end();
  Serial.println("=== End RFID Send ===");
}

//------------------------------------------------------------------------------------------

void checkForBookInfo() {
  if (!firebaseReady || bookBarcode.length() == 0) {
    Serial.println("checkForBookInfo: Firebase not ready or bookBarcode empty");
    return;
  }
  
  Serial.println("=== Checking for book info in barcodes collection ===");
  Serial.print("Barcode: ");
  Serial.println(bookBarcode);
  
  String url = String(FIRESTORE_URL) + "/barcodes?key=" + String(FIREBASE_API_KEY);
  
  Serial.print("URL: ");
  Serial.println(url);
  
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(url);
  
  int httpResponseCode = http.GET();
  
  Serial.print("HTTP Response Code: ");
  Serial.println(httpResponseCode);
  
  if (httpResponseCode == 200) {
    String response = http.getString();
    Serial.println("=== Barcodes response received ===");
    Serial.print("Response length: ");
    Serial.println(response.length());
    
    // Normalize barcode for comparison
    String search = bookBarcode;
    search.replace(" ", "");
    search.toUpperCase();
    
    Serial.print("Searching for normalized barcode: ");
    Serial.println(search);
    
    int docsPos = response.indexOf("\"documents\"");
    if (docsPos == -1) {
      Serial.println("ERROR: No 'documents' array found");
      bookName = "No books in database";
      bookAuthor = "";
      waitingForBookInfo = false;
      updateBookInputDisplay();
      http.end();
      return;
    }
    
    Serial.println("Found 'documents' array in response");
    
    bool found = false;
    int docStart = response.indexOf("{", docsPos);
    int checked = 0;
    
    while (docStart != -1 && !found && checked < 50) {
      checked++;
      int docEnd = docStart + 1;
      int braces = 1;
      while (docEnd < response.length() && braces > 0) {
        if (response.charAt(docEnd) == '{') braces++;
        if (response.charAt(docEnd) == '}') braces--;
        docEnd++;
      }
      
      if (braces == 0) {
        String doc = response.substring(docStart, docEnd);
        unsigned int docLen = doc.length();
        
        Serial.print("Document ");
        Serial.print(checked);
        Serial.println(" extracted");
        
        // Look for "barcode" field - but make sure it's the actual barcode field
        // We need to find the field that contains the barcode NUMBER, not "type": "barcode"
        
        // Strategy: Look for all occurrences of "barcode" and check each one
        int barcodeField = -1;
        int searchPos = 0;
        
        while (searchPos < doc.length()) {
          int tempPos = doc.indexOf("\"barcode\"", searchPos);
          if (tempPos == -1) break;
          
          Serial.print("Found 'barcode' at position ");
          Serial.println(tempPos);
          
          // Extract a section around this position to check context
          int checkStart = (tempPos > 100) ? tempPos - 100 : 0;
          unsigned int checkEnd = (unsigned int)tempPos + 200;
          if (checkEnd > docLen) checkEnd = docLen;
          String context = doc.substring(checkStart, checkEnd);
          
          // Print context for debugging
          Serial.println("Context around 'barcode':");
          Serial.println(context);
          
          // Check if this "barcode" is actually a field name for barcode data
          // Look for the pattern: "barcode": { "stringValue": "number" }
          // Extract what comes after "barcode"
          unsigned int afterBarcode = (unsigned int)tempPos + 300;
          if (afterBarcode > docLen) afterBarcode = docLen;
          String section = doc.substring(tempPos, afterBarcode);
          
          int svPos = section.indexOf("\"stringValue\"");
          if (svPos != -1) {
            int colonPos = section.indexOf(":", svPos);
            if (colonPos != -1) {
              int quotePos = section.indexOf("\"", colonPos + 1);
              if (quotePos != -1) {
                int valueStart = quotePos + 1;
                int valueEnd = section.indexOf("\"", valueStart);
                
                if (valueEnd > valueStart) {
                  String value = section.substring(valueStart, valueEnd);
                  Serial.print("Found stringValue: '");
                  Serial.print(value);
                  Serial.println("'");
                  
                  // Check if this value is "barcode" (the type field) or a number (the actual barcode)
                  if (value != "barcode" && value.length() > 0) {
                    // This looks like an actual barcode value!
                    String normalizedValue = value;
                    normalizedValue.replace(" ", "");
                    normalizedValue.toUpperCase();
                    
                    Serial.print("Normalized value: '");
                    Serial.print(normalizedValue);
                    Serial.print("' vs searching for: '");
                    Serial.print(search);
                    Serial.println("'");
                    
                    if (normalizedValue == search) {
                      Serial.println("*** BARCODE MATCH FOUND! ***");
                      found = true;
                      
                      // Extract title
                      int titlePos = doc.indexOf("\"title\"");
                      if (titlePos == -1) titlePos = doc.indexOf("\"itemTitle\"");
                      if (titlePos != -1) {
                        unsigned int titleLimit = (unsigned int)titlePos + 300;
                        if (titleLimit > docLen) titleLimit = docLen;
                        String titleSec = doc.substring(titlePos, titleLimit);
                        int titleSv = titleSec.indexOf("\"stringValue\"");
                        if (titleSv != -1) {
                          int titleColon = titleSec.indexOf(":", titleSv);
                          int titleQuote = titleSec.indexOf("\"", titleColon + 1);
                          if (titleQuote != -1) {
                            int titleStart = titleQuote + 1;
                            int titleEnd = titleSec.indexOf("\"", titleStart);
                            if (titleEnd > titleStart) {
                              bookName = titleSec.substring(titleStart, titleEnd);
                              Serial.print("Title: ");
                              Serial.println(bookName);
                            }
                          }
                        }
                      }
                      
                      // Extract author
                      int authorPos = doc.indexOf("\"author\"");
                      if (authorPos == -1) authorPos = doc.indexOf("\"itemAuthor\"");
                      if (authorPos != -1) {
                        unsigned int authorLimit = (unsigned int)authorPos + 300;
                        if (authorLimit > docLen) authorLimit = docLen;
                        String authorSec = doc.substring(authorPos, authorLimit);
                        int authorSv = authorSec.indexOf("\"stringValue\"");
                        if (authorSv != -1) {
                          int authorColon = authorSec.indexOf(":", authorSv);
                          int authorQuote = authorSec.indexOf("\"", authorColon + 1);
                          if (authorQuote != -1) {
                            int authorStart = authorQuote + 1;
                            int authorEnd = authorSec.indexOf("\"", authorStart);
                            if (authorEnd > authorStart) {
                              bookAuthor = authorSec.substring(authorStart, authorEnd);
                              Serial.print("Author: ");
                              Serial.println(bookAuthor);
                            }
                          }
                        }
                      }
                      
                      waitingForBookInfo = false;
                      updateBookInputDisplay();
                      break; // Found the barcode, stop searching
                    }
                  } else {
                    Serial.println("This is the 'type' field, not the barcode field. Continuing search...");
                  }
                }
              }
            }
          }
          
          searchPos = tempPos + 10;
        }
        
        if (found) break; // Stop checking documents if we found it
      }
      
      docStart = response.indexOf("{", docEnd);
    }
    
    Serial.print("Checked ");
    Serial.print(checked);
    Serial.println(" documents");
    
    if (!found) {
      Serial.println("Barcode not found in database");
      bookName = "Book not found";
      bookAuthor = "Please register barcode";
      waitingForBookInfo = false;
      updateBookInputDisplay();
    }
  } else {
    Serial.print("ERROR: HTTP request failed with code: ");
    Serial.println(httpResponseCode);
    
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.print("Error response: ");
      Serial.println(response);
    }
    
    bookName = "Database error";
    bookAuthor = "";
    waitingForBookInfo = false;
    updateBookInputDisplay();
  }
  
  http.end();
  Serial.println("=== End book info check ===");
}
    Serial.println("=== Barcodes response received ===");
    Serial.print("Response length: ");
    Serial.println(response.length());
    
    // Normalize barcode for comparison
    String search = bookBarcode;
    search.replace(" ", "");
    search.toUpperCase();
    
    Serial.print("Searching for normalized barcode: ");
    Serial.println(search);
    
    int docsPos = response.indexOf("\"documents\"");
    if (docsPos == -1) {
      Serial.println("ERROR: No 'documents' array found");
      bookName = "No books in database";
      bookAuthor = "";
      waitingForBookInfo = false;
      updateBookInputDisplay();
      http.end();
      return;
    }
    
    Serial.println("Found 'documents' array in response");
    
    bool found = false;
    int docStart = response.indexOf("{", docsPos);
    int checked = 0;
    
    while (docStart != -1 && !found && checked < 50) {
      checked++;
      int docEnd = docStart + 1;
      int braces = 1;
      while (docEnd < response.length() && braces > 0) {
        if (response.charAt(docEnd) == '{') braces++;
        if (response.charAt(docEnd) == '}') braces--;
        docEnd++;
      }
      
      if (braces == 0) {
        String doc = response.substring(docStart, docEnd);
        unsigned int docLen = doc.length();
        
        Serial.print("Document ");
        Serial.print(checked);
        Serial.println(" extracted");
        
        // Look for "barcode" field - but make sure it's the actual barcode field
        // We need to find the field that contains the barcode NUMBER, not "type": "barcode"
        
        // Strategy: Look for all occurrences of "barcode" and check each one
        int barcodeField = -1;
        int searchPos = 0;
        
        while (searchPos < doc.length()) {
          int tempPos = doc.indexOf("\"barcode\"", searchPos);
          if (tempPos == -1) break;
          
          Serial.print("Found 'barcode' at position ");
          Serial.println(tempPos);
          
          // Extract a section around this position to check context
          int checkStart = (tempPos > 100) ? tempPos - 100 : 0;
          unsigned int checkEnd = (unsigned int)tempPos + 200;
          if (checkEnd > docLen) checkEnd = docLen;
          String context = doc.substring(checkStart, checkEnd);
          
          // Print context for debugging
          Serial.println("Context around 'barcode':");
          Serial.println(context);
          
          // Check if this "barcode" is actually a field name for barcode data
          // Look for the pattern: "barcode": { "stringValue": "number" }
          // Extract what comes after "barcode"
          unsigned int afterBarcode = (unsigned int)tempPos + 300;
          if (afterBarcode > docLen) afterBarcode = docLen;
          String section = doc.substring(tempPos, afterBarcode);
          
          int svPos = section.indexOf("\"stringValue\"");
          if (svPos != -1) {
            int colonPos = section.indexOf(":", svPos);
            if (colonPos != -1) {
              int quotePos = section.indexOf("\"", colonPos + 1);
              if (quotePos != -1) {
                int valueStart = quotePos + 1;
                int valueEnd = section.indexOf("\"", valueStart);
                
                if (valueEnd > valueStart) {
                  String value = section.substring(valueStart, valueEnd);
                  Serial.print("Found stringValue: '");
                  Serial.print(value);
                  Serial.println("'");
                  
                  // Check if this value is "barcode" (the type field) or a number (the actual barcode)
                  if (value != "barcode" && value.length() > 0) {
                    // This looks like an actual barcode value!
                    String normalizedValue = value;
                    normalizedValue.replace(" ", "");
                    normalizedValue.toUpperCase();
                    
                    Serial.print("Normalized value: '");
                    Serial.print(normalizedValue);
                    Serial.print("' vs searching for: '");
                    Serial.print(search);
                    Serial.println("'");
                    
                    if (normalizedValue == search) {
                      Serial.println("*** BARCODE MATCH FOUND! ***");
                      found = true;
                      
                      // Extract title
                      int titlePos = doc.indexOf("\"title\"");
                      if (titlePos == -1) titlePos = doc.indexOf("\"itemTitle\"");
                      if (titlePos != -1) {
                        unsigned int titleLimit = (unsigned int)titlePos + 300;
                        if (titleLimit > docLen) titleLimit = docLen;
                        String titleSec = doc.substring(titlePos, titleLimit);
                        int titleSv = titleSec.indexOf("\"stringValue\"");
                        if (titleSv != -1) {
                          int titleColon = titleSec.indexOf(":", titleSv);
                          int titleQuote = titleSec.indexOf("\"", titleColon + 1);
                          if (titleQuote != -1) {
                            int titleStart = titleQuote + 1;
                            int titleEnd = titleSec.indexOf("\"", titleStart);
                            if (titleEnd > titleStart) {
                              bookName = titleSec.substring(titleStart, titleEnd);
                              Serial.print("Title: ");
                              Serial.println(bookName);
                            }
                          }
                        }
                      }
                      
                      // Extract author
                      int authorPos = doc.indexOf("\"author\"");
                      if (authorPos == -1) authorPos = doc.indexOf("\"itemAuthor\"");
                      if (authorPos != -1) {
                        unsigned int authorLimit = (unsigned int)authorPos + 300;
                        if (authorLimit > docLen) authorLimit = docLen;
                        String authorSec = doc.substring(authorPos, authorLimit);
                        int authorSv = authorSec.indexOf("\"stringValue\"");
                        if (authorSv != -1) {
                          int authorColon = authorSec.indexOf(":", authorSv);
                          int authorQuote = authorSec.indexOf("\"", authorColon + 1);
                          if (authorQuote != -1) {
                            int authorStart = authorQuote + 1;
                            int authorEnd = authorSec.indexOf("\"", authorStart);
                            if (authorEnd > authorStart) {
                              bookAuthor = authorSec.substring(authorStart, authorEnd);
                              Serial.print("Author: ");
                              Serial.println(bookAuthor);
                            }
                          }
                        }
                      }
                      
                      waitingForBookInfo = false;
                      updateBookInputDisplay();
                      break; // Found the barcode, stop searching
                    }
                  } else {
                    Serial.println("This is the 'type' field, not the barcode field. Continuing search...");
                  }
                }
              }
            }
          }
          
          searchPos = tempPos + 10;
        }
        
        if (found) break; // Stop checking documents if we found it
      }
      
      docStart = response.indexOf("{", docEnd);
    }
    
    Serial.print("Checked ");
    Serial.print(checked);
    Serial.println(" documents");
    
    if (!found) {
      Serial.println("Barcode not found in database");
      bookName = "Book not found";
      bookAuthor = "Please register barcode";
      waitingForBookInfo = false;
      updateBookInputDisplay();
    }
  } else {
    Serial.print("ERROR: HTTP request failed with code: ");
    Serial.println(httpResponseCode);
    
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.print("Error response: ");
      Serial.println(response);
    }
    
    bookName = "Database error";
    bookAuthor = "";
    waitingForBookInfo = false;
    updateBookInputDisplay();
  }
  
  http.end();
  Serial.println("=== End book info check ===");
}

//-------------------------------------------------------------------------------

void cleanupSyncDocument(String sessionId) {
  if (!firebaseReady || sessionId.length() == 0) return;

  Serial.println("=== Cleaning up sync document ===");
  Serial.print("Session ID: "); Serial.println(sessionId);

  String delUrl = String(FIRESTORE_URL) + "/sync/" + sessionId + "?key=" + String(FIREBASE_API_KEY);

  HTTPClient http;
  http.setTimeout(10000);
  http.begin(delUrl);
  int delCode = http.sendRequest("DELETE");

  if (delCode == 200 || delCode == 204) {
    Serial.println("Sync document deleted successfully");
  } else {
    Serial.print("Failed to delete sync document, code: "); Serial.println(delCode);
  }
  http.end();
}
//------------------------------------------------------------------------------------------

void sendCheckoutToFirebase() {
  if (!firebaseReady || rfidUID.length() == 0 || bookBarcode.length() == 0) {
    sendingToFirebase = false;
    return;
  }
  
  Serial.println("Sending to Firestore...");
  
  // Create unique document ID and determine collection based on action type
  String collection = (actionType == "return") ? "returns" : "checkouts";
  String documentId = collection + "_" + String(millis());
  String url = String(FIRESTORE_URL) + "/" + collection + "?documentId=" + documentId + "&key=" + String(FIREBASE_API_KEY);
  
  // Create Firestore document structure
  // Firestore expects fields in a specific format: { fields: { fieldName: { stringValue: "value" } } }
  // Escape JSON strings to prevent issues
  String escapedRfid = rfidUID;
  escapedRfid.replace("\"", "\\\"");
  String escapedBarcode = bookBarcode;
  escapedBarcode.replace("\"", "\\\"");
  String escapedUserName = userName.length() > 0 ? userName : "Unknown";
  escapedUserName.replace("\"", "\\\"");
  String escapedBookName = bookName.length() > 0 ? bookName : bookBarcode;
  escapedBookName.replace("\"", "\\\"");
  
  String jsonPayload = "{";
  jsonPayload += "\"fields\": {";
  jsonPayload += "\"cardId\": {\"stringValue\": \"" + escapedRfid + "\"},";
  jsonPayload += "\"barcode\": {\"stringValue\": \"" + escapedBarcode + "\"},";
  jsonPayload += "\"userName\": {\"stringValue\": \"" + escapedUserName + "\"},";
  jsonPayload += "\"studentId\": {\"stringValue\": \"" + studentId + "\"},";
  jsonPayload += "\"email\": {\"stringValue\": \"" + studentId + "\"},";  // Explicit email field for mail system
  jsonPayload += "\"bookName\": {\"stringValue\": \"" + escapedBookName + "\"},";
  if (bookAuthor.length() > 0) {
    String escapedAuthor = bookAuthor;
    escapedAuthor.replace("\"", "\\\"");
    jsonPayload += "\"bookAuthor\": {\"stringValue\": \"" + escapedAuthor + "\"},";
  }
  jsonPayload += "\"timestamp\": {\"timestampValue\": \"" + getCheckoutTimestampRFC3339() + "\"},";
  jsonPayload += "\"actionType\": {\"stringValue\": \"" + actionType + "\"},";
  jsonPayload += "\"type\": {\"stringValue\": \"" + collection + "\"}";
  jsonPayload += "}";
  jsonPayload += "}";
  
  Serial.println("URL: " + url);
  Serial.println("Payload: " + jsonPayload);
  
  // DEBUG: Print key fields being sent
  Serial.print("=== SENDING TO FIRESTORE ===");
  Serial.print(" | actionType: ");
  Serial.print(actionType);
  Serial.print(" | cardId: ");
  Serial.print(escapedRfid);
  Serial.print(" | email/studentId: ");
  Serial.print(studentId.length() > 0 ? "YES (" + studentId + ")" : "EMPTY!");
  Serial.println();
  
  // Create HTTP client
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  // Send POST request
  int httpResponseCode = http.POST(jsonPayload);
  
  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    
    if (httpResponseCode == 200 || httpResponseCode == 201) {
      String response = http.getString();
      Serial.println("Response: " + response);
      Serial.println("Successfully saved to Firestore!");
      sendingToFirebase = false;
      
      // Update book status based on action type
      updateBookStatus(bookBarcode, actionType);
      
      // Update display
      tft.setTextDatum(TC_DATUM);
      tft.setTextColor(0x0019, TFT_WHITE);  // Navy blue
      tft.fillRect(10, 220, 220, 20, TFT_WHITE);
      tft.setFreeFont(&FreeSans9pt7b);
      tft.drawString("Saved to cloud!", 120, 235);
    } else {
      Serial.print("Error code: ");
      Serial.println(httpResponseCode);
      String response = http.getString();
      Serial.println("Response: " + response);
      sendingToFirebase = false;
      
      // Update display with error
      tft.setTextDatum(TC_DATUM);
      tft.setTextColor(TFT_RED, TFT_WHITE);  // Red on white
      tft.fillRect(10, 220, 220, 20, TFT_WHITE);
      tft.setFreeFont(&FreeSans9pt7b);
      tft.drawString("Save failed", 120, 235);
    }
  } else {
    Serial.print("Error sending request: ");
    Serial.println(httpResponseCode);
    sendingToFirebase = false;
    
    // Update display with error
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_RED, TFT_WHITE);  // Red on white
    tft.fillRect(10, 220, 220, 20, TFT_WHITE);
    tft.setFreeFont(&FreeSans9pt7b);
    tft.drawString("Save failed", 120, 235);
  }
  
  http.end();
}

//------------------------------------------------------------------------------------------

void updateBookStatus(String barcode, String action) {
  if (!firebaseReady || barcode.length() == 0) return;

  Serial.println("=== Updating book status ===");

  String getUrl = String(FIRESTORE_URL) + "/barcodes?key=" + String(FIREBASE_API_KEY);
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(getUrl);
  int getCode = http.GET();

  if (getCode != 200) {
    Serial.print("Failed to get barcodes: "); Serial.println(getCode);
    http.end();
    return;
  }

  String response = http.getString();
  http.end();

  String searchBarcode = barcode;
  searchBarcode.replace(" ", "");
  searchBarcode.toUpperCase();

  int docsPos = response.indexOf("\"documents\"");
  if (docsPos == -1) { Serial.println("No barcode documents found"); return; }

  int docStart = response.indexOf("{", docsPos);
  int checked = 0;
  while (docStart != -1 && checked < 100) {
    checked++;
    int docEnd = docStart + 1;
    int braces = 1;
    while (docEnd < response.length() && braces > 0) {
      if (response.charAt(docEnd) == '{') braces++;
      if (response.charAt(docEnd) == '}') braces--;
      docEnd++;
    }

    String doc = response.substring(docStart, docEnd);

    // Extract document ID from "name" field
    String docId = "";
    int namePos = doc.indexOf("\"name\"");
    if (namePos != -1) {
      int nameValStart = doc.indexOf("\"", namePos + 6) + 1;
      int nameValEnd   = doc.indexOf("\"", nameValStart);
      if (nameValEnd > nameValStart) {
        String fullPath = doc.substring(nameValStart, nameValEnd);
        int lastSlash = fullPath.lastIndexOf("/");
        if (lastSlash != -1) docId = fullPath.substring(lastSlash + 1);
      }
    }

    // Walk all "barcode" field occurrences and check stringValue
    int barcodePos = doc.indexOf("\"barcode\"");
    while (barcodePos != -1) {
      int svPos = doc.indexOf("\"stringValue\"", barcodePos);
      if (svPos == -1 || svPos > barcodePos + 200) break;
      int valStart = doc.indexOf("\"", svPos + 14) + 1;
      int valEnd   = doc.indexOf("\"", valStart);
      if (valEnd > valStart) {
        String val = doc.substring(valStart, valEnd);
        val.replace(" ", "");
        val.toUpperCase();

        if (val == searchBarcode && docId.length() > 0) {
          String newStatus = (action == "return") ? "available" : "borrowed";
          Serial.print("Updating book '"); Serial.print(docId);
          Serial.print("' status to: "); Serial.println(newStatus);

          // updateMask ensures ONLY the status field is written — other fields are untouched
          String updateUrl = String(FIRESTORE_URL) + "/barcodes/" + docId
            + "?updateMask.fieldPaths=status&key=" + String(FIREBASE_API_KEY);

          String payload = "{\"fields\":{\"status\":{\"stringValue\":\"" + newStatus + "\"}}}";

          HTTPClient upHttp;
          upHttp.setTimeout(10000);
          upHttp.begin(updateUrl);
          upHttp.addHeader("Content-Type", "application/json");
          int upCode = upHttp.PATCH(payload);
          Serial.print("Book status update response code: "); Serial.println(upCode);

          if (upCode == 200) {
            Serial.println("Book status updated successfully!");
          } else {
            String upResponse = upHttp.getString();
            Serial.print("Update failed: "); Serial.println(upResponse);
          }
          upHttp.end();
          return;
        }
      }
      barcodePos = doc.indexOf("\"barcode\"", barcodePos + 10);
    }

    docStart = response.indexOf("{", docEnd);
  }

  Serial.println("Book not found in barcodes collection — status not updated");
}

//------------------------------------------------------------------------------------------

String getTimestamp() {
  // Get current timestamp (Unix time)
  // Note: For accurate time, you'd need NTP sync
  time_t now = time(nullptr);
  if (now > 1600000000) {
    return String((unsigned long)now);
  }

  // Fallback to uptime seconds if NTP not available
  unsigned long currentMillis = millis();
  unsigned long currentSeconds = currentMillis / 1000;
  return String(currentSeconds);
}

//------------------------------------------------------------------------------------------

String getFirestoreTimestamp() {
  // Original placeholder implementation kept for non-checkout timestamps
  // Firestore expects timestamp in RFC3339 format: "2024-01-01T00:00:00Z"
  // The checkout flow uses a dedicated accurate timestamp helper.
  return String("2024-01-01T00:00:00Z");
}

//------------------------------------------------------------------------------------------
// Checkout-specific RFC3339 timestamp helper (uses NTP if available)
String getCheckoutTimestampRFC3339() {
  time_t now = time(nullptr);
  char buf[32] = {0};

  if (now > 1600000000) {
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    return String(buf);
  }

  // Fallback: best-effort approximate timestamp using uptime (not real UTC)
  unsigned long seconds = millis() / 1000;
  unsigned long approx = seconds;
  struct tm fakeTm;
  gmtime_r((time_t *)&approx, &fakeTm);
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &fakeTm);
  return String(buf);
}


//------------------------------------------------------------------------------------------
