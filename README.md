# Library Self-Checkout System

An embedded self-checkout kiosk for a library. Patrons tap an RFID card on an ESP32 touchscreen terminal, a barcode scanner identifies the book, and the checkout is recorded in Firebase Firestore. Librarians manage cards and books through a web admin dashboard, and patrons get borrow, return and due-date emails automatically.

## How it works

```
 ┌────────────────────┐         ┌──────────────────────┐         ┌────────────────────┐
 │  ESP32 kiosk       │  REST   │  Firebase Firestore  │  SDK    │  Web admin         │
 │  TFT touchscreen   │◄───────►│  /sync (live channel)│◄───────►│  register cards,   │
 │  MFRC522 RFID      │         │  /cards  /barcodes   │         │  register books,   │
 └────────────────────┘         │  /checkouts /returns │         │  barcode relay     │
                                └──────────┬───────────┘         └────────────────────┘
                                           │ triggers
                                ┌──────────▼───────────┐
                                │  Cloud Functions     │──► /mail (borrow & return emails)
                                │  checkout / return / │──► /reminders (due-date emails)
                                │  daily reminders     │
                                └──────────────────────┘
```

1. The ESP32 reads an RFID card and checks it against the registered `cards`.
2. The librarian's laptop has the USB barcode scanner attached. The **Barcode Relay** page forwards each scan to the ESP32 through the `/sync` collection.
3. The ESP32 writes a `checkouts` or `returns` document.
4. Cloud Functions react to that document and queue the emails.

## Repository layout

```
.
├── firmware/self_checkout/   ESP32 Arduino sketch (touch UI, RFID, Firestore REST client)
│   ├── self_checkout.ino
│   └── secrets.example.h     Template for WiFi credentials
├── web/                      Static admin dashboard (HTML / CSS / ES-module JS)
│   ├── *.html                dashboard, register-cards, register-books, relay, data
│   ├── css/
│   └── js/                   app.js, firebase-config.js
├── functions/                Firebase Cloud Functions (Node 20)
├── firestore.rules           Firestore security rules
├── firebase.json
└── .firebaserc
```

## Hardware

- ESP32 development board
- 240×320 TFT touchscreen (driven with [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI))
- MFRC522 RFID reader (SPI; `SS` = GPIO 21, `RST` = GPIO 22)
- USB barcode scanner, plugged into the laptop that runs the Barcode Relay page

## Setup

### 1. Firebase project

1. Create a Firebase project and a Firestore database.
2. Put your own web config in `web/js/firebase-config.js` and update `.firebaserc` with your project ID.
3. Deploy the rules and functions:
   ```bash
   cd functions && npm install && cd ..
   firebase deploy --only firestore:rules,functions
   ```
4. Emails are queued as documents in the `mail` collection, which is the format the Firebase **Trigger Email** extension reads. Install and configure that extension to actually send them.

### 2. Web dashboard

The pages use ES modules, so they must be served over HTTP rather than opened from `file://`:

```bash
python -m http.server 8000 --directory web
# then open http://localhost:8000
```

### 3. ESP32 firmware

1. Install the ESP32 board package and the `TFT_eSPI` and `MFRC522` libraries in the Arduino IDE. Configure your display pins in TFT_eSPI's `User_Setup.h`.
2. In `firmware/self_checkout/`, copy `secrets.example.h` to `secrets.h` and fill in your WiFi name and password. `secrets.h` is gitignored.
3. In `self_checkout.ino`, set `FIREBASE_PROJECT_ID`, `FIREBASE_API_KEY` and `FIRESTORE_URL` to match your project.
4. Open `self_checkout.ino`, select your board and upload.

## Cloud Functions

| Function | Trigger | What it does |
| --- | --- | --- |
| `onCheckoutCreated` | New doc in `checkouts` | Queues a borrow receipt email and creates a due-date reminder |
| `onReturnCreated` | New doc in `returns` | Queues a return confirmation email and clears the matching checkout |
| `processReminders` | Every 24 hours | Sends due-date reminder emails that are ready |
