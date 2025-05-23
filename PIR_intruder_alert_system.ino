#include <LiquidCrystal.h>
#include <LowPower.h>
#include <Ultrasonic.h>
#include <EEPROM.h>
#include <TimerOne.h>
#include <PinChangeInterrupt.h>


// EEPROM Adresses
#define violations_NUM 0
#define violation_DAYS 1

// LCD pins
#define RS 13
#define EN 12
#define D4 8
#define D5 9
#define D6 10
#define D7 11
#define backlight A4 

// PIR pins (external interrupts)
#define P1 3 // INT1
#define P2 2 // INT0

// Buzzer pins
#define B1 5
#define B2 4

// LEDs for Handicaps
#define R 7
#define B 6

// Ultrasonic pins
#define echo A0 
#define trig A1

// Bluetooth Module pins
#define state A5


// Declarations
LiquidCrystal lcd(RS, EN, D4, D5, D6, D7);
Ultrasonic ultrasonic(trig, echo);

// variables
volatile int validation_counter = 0; // To count valid readings
volatile int toggle_counter = 0; // Count toggles for 5s (10: 0.5s * 10 = 5s)
volatile int timer_state = 0; // 0 = idle, 1 = validating, 2 = toggling

volatile int violations_num = 0;
volatile int violation_days = 0;

volatile bool ledState = false;
volatile bool buzzerState = false;

volatile bool received = false;
volatile bool valid_code = false;
volatile const int code = 5591;
volatile int challenge;
volatile int receivedCode;
volatile char XOR_KEY = 'K';

void incViolations()
{
  // the system can store up to 255 * 255 violations (days * violations per day) = 65025 violations (255 days = 8.5 months)
  /*
  * 1. add violations number to the total number of violations in the EEPROM every 20 violations
  * 2. increment the number of days if the number of violations exceeds 255
  * 3. increment the number of violations
  */
  if (violations_num >= 20) // add vilations number every 20 violations
  {
    violations_num = 0;
    EEPROM.write(violations_NUM, EEPROM.read(violations_NUM) + violation_days);
  }
  // assuming that the number of violations per day will not exceed 255
  // if it exceeds 255, the number of violations will be reset to 0 and the number of days will be incremented
  if (violations_num >= 255)
  {
    violations_num = 0;
    violation_days++;
    EEPROM.write(violations_NUM, 0);
    EEPROM.write(violation_DAYS, EEPROM.read(violation_DAYS) + violation_days);
  }
  else
  {
    violations_num++;
  }
}

int readViolations()
{
  return violations_num; // return the number of violations in the current day
}

void pin_setup()
{
  pinMode(backlight, OUTPUT);

  pinMode(R, OUTPUT);
  pinMode(B, OUTPUT);

  pinMode(B1, OUTPUT);
  pinMode(B2, OUTPUT);

  pinMode(P1, INPUT);
  pinMode(P2, INPUT);

  pinMode(state, INPUT);
}

void welcomeMessage()
{
  lcd.setCursor(4, 0);
  lcd.print("Welcome!");
  delay(2000);
  lcd.clear();
}

void updateViolations()
{
  lcd.setCursor(5, 1);
  lcd.print("   "); // clear previous number
  lcd.setCursor(5, 1);
  lcd.print(readViolations()); // print new number
}


// XOR Encryption/Decryption
String xorEncryptDecrypt(String data, char key) {
  String result = "";
  for (int i = 0; i < data.length(); i++) {
    result += (char)(data[i] ^ key); // XOR each character with the key
  }
  return result;
}


void setup()
{
  int old_violations = EEPROM.read(violations_NUM);
  int days = EEPROM.read(violation_DAYS);
  // read the number of violations from the EEPROM for power failure recovery
  if (old_violations || days) 
  {
    violations_num = old_violations;
    violation_days = days;
  }

  pin_setup();
  Serial.begin(9600);

  lcd.begin(16, 2);
  delay(1000);
  welcomeMessage();
  lcd.print("No. violations:");
  updateViolations();
  lcd.setCursor(10, 1);
  lcd.print("val:");

  delay(5000);

  attachInterrupt(digitalPinToInterrupt(P1), motionISR, RISING); // attach interrupt for PIR sensor 1
  attachInterrupt(digitalPinToInterrupt(P2), motionISR, RISING); // attach interrupt for PIR sensor 2

  attachPCINT(digitalPinToPCINT(state), connectedISR, RISING); // interrupt for bluetooth state
  attachPCINT(digitalPinToPCINT(0), receivedISR, LOW); // interrupt for received data

  Timer1.initialize(500000); // Initialize Timer1 to 0.5s (500,000 microseconds)
  Timer1.stop(); // Stop the timer
  Timer1.attachInterrupt(timerISR); // Attach the interrupt service routine
  
}

void loop()
{
  // if the system is idle and the bluetooth module is disconnected or no data is received, the system will go to sleep
  if (timer_state == 0 && (!received || digitalRead(state) == LOW))
  {
    valid_code = false;
    received = false;
    // Turn off the LCD and backlight
    lcd.noDisplay();
    digitalWrite(backlight, LOW);

    Serial.println("Going to sleep...");

    // Put the Arduino into power-down mode until an interrupt occurs
    LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);

    Serial.println("Woke up!");
  }

  if (received)
  {
    if (valid_code)
    {
      String m1 = "Days = " + String(violation_days);
      String m2 = "All Violations = " + String(violation_days * 255 + violations_num);
      String m3 = "Today's Violations = " + String(violations_num);

      Serial.println(xorEncryptDecrypt(m1, XOR_KEY));
      Serial.println(xorEncryptDecrypt(m2, XOR_KEY));
      Serial.println(xorEncryptDecrypt(m3, XOR_KEY));
    }
    else
    {
      Serial.println(xorEncryptDecrypt("Invalid answer", XOR_KEY));
      received = false;
    }
  }

  // a small delay to debounce the interrupt pin
  delay(100);
}

void motionISR()
{
  // Disable interrupts for both sensors
  detachInterrupt(digitalPinToInterrupt(P1));
  detachInterrupt(digitalPinToInterrupt(P2));

  // Turn on the LCD and backlight
  lcd.display();
  digitalWrite(backlight, HIGH);

  validation_counter = 0; // Reset validation counter
  timer_state = 1;     // Set timer to validation mode
  Timer1.start();         // Start the timer
}


void timerISR()
{
  if (timer_state == 1) // validation mode
  {
    if ((digitalRead(P1) || digitalRead(P2)) && ultrasonic.read() < 30)
    {
      validation_counter++;
    }
    else
    {
      // Re-enable interrupts
      attachInterrupt(digitalPinToInterrupt(P1), motionISR, RISING);
      attachInterrupt(digitalPinToInterrupt(P2), motionISR, RISING);

      validation_counter = 0; // Reset counter if reading is invalid
      timer_state = 0; // Back to idle
    }

    if (validation_counter >= 3) // Valid motion detected
    {
      validation_counter = 0;
      timer_state = 2; // Switch to toggling mode
      toggle_counter = 0;
      incViolations();
      updateViolations();
    }

    // Display the current validation count
    lcd.setCursor(14, 1);
    lcd.print("  "); // Clear previous number
    lcd.setCursor(14, 1);
    lcd.print(validation_counter);
  }
  else if (timer_state == 2) // toggling mode
  {
    if (toggle_counter < 10)
    {
      ledState = !ledState;
      buzzerState = !buzzerState;

      digitalWrite(R, ledState);
      digitalWrite(B, !ledState);
      digitalWrite(B1, buzzerState);
      digitalWrite(B2, !buzzerState);

      toggle_counter++;
    }
    else
    {
      // Turn off LEDs and buzzer
      digitalWrite(R, LOW);
      digitalWrite(B, LOW);
      digitalWrite(B1, LOW);
      digitalWrite(B2, LOW);

      Timer1.stop(); // Stop the timer
      timer_state = 0; // Back to idle

      // Re-enable interrupts
      attachInterrupt(digitalPinToInterrupt(P1), motionISR, RISING);
      attachInterrupt(digitalPinToInterrupt(P2), motionISR, RISING);
    }
  }
}

void connectedISR()
{
  // Turn on the LCD and backlight
  lcd.display();
  digitalWrite(backlight, HIGH);

  challenge = random(1000, 9999); // generate a random 4-digit number
  String m1 = "Challenge number: " + String(challenge);
  Serial.println(xorEncryptDecrypt(m1, XOR_KEY));
  Serial.println(xorEncryptDecrypt("Enter the answer: ", XOR_KEY));
}

void serialEvent()
{
  if (Serial.available()) {
    String receivedString = Serial.readStringUntil('\n');
    receivedCode = receivedString.toInt();
    valid_code = (receivedCode == (code ^ challenge));
    received = true;
  }
}

void receivedISR()
{
  lcd.display();
  digitalWrite(backlight, HIGH);
}