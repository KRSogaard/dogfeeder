#include <ThreeWire.h>
#include <RtcDS1302.h>
#include <LCD_I2C.h>
#include <EEPROM.h>

LCD_I2C lcd(0x27, 16, 2);  // Default address of most PCF8574 modules, change according
ThreeWire myWire(4, 5, 2); // IO, SCLK, CE
RtcDS1302<ThreeWire> Rtc(myWire);

class FeedingData
{
private:
  unsigned long _time;
  int _portions;

public:
  FeedingData(unsigned long time, int portions);
  FeedingData();
  unsigned long getTime();
  int getPortions();
  void setTime(unsigned long time);
  void setPortions(int portions);
};

FeedingData::FeedingData(unsigned long time, int portions)
{
  _time = time;
  _portions = portions;
}
FeedingData::FeedingData()
{
  _time = 0;
  _portions = 0;
}

unsigned long FeedingData::getTime()
{
  return _time;
}

int FeedingData::getPortions()
{
  return _portions;
}

void FeedingData::setTime(unsigned long time)
{
  _time = time;
}

void FeedingData::setPortions(int portions)
{
  _portions = portions;
}

const int sensorPin = A1;        // select the input pin for LDR
const int buttonSet = 8;
const int buttonPlus = 9;
const int buttonMinus = 10;
const int switchButton = 7;
const int engineOut = 12;
const int buzzer = 11;
const int testLED = 6;

// 0 = BOOTING
// 1 = IDLE STATE
// 100 = MAIN MENU
// 200 = TIME MENU
// 300 = FEEDING MENU
// 310 = NEW FEEDING MENU
// 350 = DELETE FEEDING MENU
// 999 = CLOCK ERROR STATE
int state = 0;
int prevState = 0;
RtcDateTime now;


int sensorValue = 0;       // variable to store the value coming from the sensor
const int maxSensorValue = 60;

bool valSet = false;
bool valPlus = false;
bool valMinus = false;
bool valSwitch = false;
bool prevButtonSet = false;
bool prevButtonPlus = false;
bool prevButtonMinus = false;
bool prevSwitch = false;
unsigned long holdMsButtonSet = 0;
unsigned long holdMsButtonPlus = 0;
unsigned long holdMsButtonMinus = 0;

const long holdTimsMs = 2000;
bool isHoldingSet = false;
bool isHoldingPlus = false;
bool isHoldingMinus = false;

int menuPage = 0;

bool blinkState = false;
unsigned long sinceLastBlink = 0;
const long blinkTime = 200;

unsigned long lastLoop = 0;
unsigned long timeSinceLastLoop;
unsigned int countDown = 0;

FeedingData feedings[15];
int feedingCount = 0;
unsigned long editFeeding = 0;
unsigned long newFeedingTime = 43200; // 12 PM
int newFeedingPortions = 1;

unsigned long wHour;
unsigned long wMin;
unsigned long currentTimeSecs;

unsigned long hourSecs;
bool shouldSave = true;

bool hasNextFeeding = false;
unsigned long nextFeeding = 0;
int nextPortions = 0;
unsigned long nextFeedingInSecs = 0;
unsigned long lastInteractionSecs = 0;

unsigned long getSeconds(unsigned long hour, unsigned long min)
{
  return hour * 3600L + min * 60L;
}

void setup()
{
  Serial.begin(9600);
  
  Serial.println("Device starting");
  pinMode(buttonSet, INPUT_PULLUP);
  pinMode(buttonPlus, INPUT_PULLUP);
  pinMode(buttonMinus, INPUT_PULLUP);
  pinMode(switchButton, INPUT);
  pinMode(engineOut, OUTPUT);
  pinMode(testLED, OUTPUT);

  // Temp
  now = Rtc.GetDateTime();
  lastInteractionSecs = getSeconds(now.Hour(), now.Minute()) + now.Second();

  // currentTimeSecs = getSeconds(now.Hour(), now.Minute());
  // currentTimeSecs += 60L;


  // feedings[0] = FeedingData(currentTimeSecs, 7);
  // feedingCount = 1;
  loadDataFromStorage();
  // Add default
  if (feedingCount == 0) {
    //Serial.println("Adding default feedings");
    addFeeding(getSeconds(8, 0), 6);
    addFeeding(getSeconds(12, 0), 6);
    addFeeding(getSeconds(17, 0), 6);
    addFeeding(getSeconds(21, 0), 5);
    dumpMemory();
  }

  lcd.begin();
  lcd.backlight();
  lcd.clear();
  lcd.print("Device starting");

  Rtc.Begin();
  setupRTC();
}

void loop()
{
  timeSinceLastLoop = millis() - lastLoop;
  sensorValue = analogRead(sensorPin);
  valSet = digitalRead(buttonSet) == HIGH;
  valPlus = digitalRead(buttonPlus) == HIGH;
  valMinus = digitalRead(buttonMinus) == HIGH;
  valSwitch = digitalRead(switchButton) == HIGH;
  calculateButtonState();
//
  Serial.print("Light: ");
  Serial.println(sensorValue);

  now = Rtc.GetDateTime();
  currentTimeSecs = getSeconds(now.Hour(), now.Minute()) + now.Second();

  if (isHoldingSet && isHoldingPlus) {
    Serial.println("Holding Set and Plus, feeding");
    feed(1);
    return;
  }

  if (valSet || valPlus || valMinus) {
    lastInteractionSecs = millis()/1000;
  }
  // Only keep light on for 1 min
  if ((millis()/1000) - lastInteractionSecs < 60) {
    lcd.backlight();
  } else {
    lcd.noBacklight();
    // Idle for 3 min go back to IDLE state
    if (state != 1 && currentTimeSecs - lastInteractionSecs < 180) {
      setIdleState();
    }
  }

  // TESTER every 10 min
  //if (now.Minute() == 0) {
    digitalWrite(testLED, HIGH);
    // We give 5 sek for the LED TO LIGHT UP
    if (now.Second() > 5 && sensorValue > maxSensorValue) {
      tone(buzzer, 500);
    } else {
      noTone(buzzer);
    }
//  } else {
//    digitalWrite(testLED, LOW);
//      noTone(buzzer);
//  }

 if (!hasNextFeeding && feedingCount > 0) {
    Serial.println("Finding next feeding");
    for (int i = 0; i < feedingCount; i++) {
      if (feedings[i].getTime() > currentTimeSecs) {
        nextFeeding = feedings[i].getTime();
        nextPortions = feedings[i].getPortions();
        hasNextFeeding = true;
        break;
      }
    }
    // Must be the next day
    if (!hasNextFeeding) {
        nextFeeding = feedings[0].getTime();
        nextPortions = feedings[0].getPortions();
        hasNextFeeding = true;
    }

    Serial.print("Next feeding: ");
    Serial.print(nextFeeding);
    Serial.print(", P: ");
    Serial.println(nextPortions);
  }

  // We have 3 seconds to find out that we have a feeding
  if (hasNextFeeding && nextFeeding <= currentTimeSecs && currentTimeSecs - nextFeeding < 3) {
    Serial.println("Starting feeding");
    lastInteractionSecs = currentTimeSecs;
    feed(nextPortions);
    hasNextFeeding = false;
    return;
  }
  if (hasNextFeeding) {
    // If next feeding is tomorrow
    if (nextFeeding < currentTimeSecs) {
//      Serial.println("Next feeding is tomorrow");
//      Serial.print("Current time:");
//      Serial.println(currentTimeSecs);
//      Serial.print("Time left today:");
//      Serial.println(86400 - currentTimeSecs);
//      Serial.print("Feeding time:");
//      Serial.println(nextFeeding);
      nextFeedingInSecs = nextFeeding + (86400 - currentTimeSecs);
//      Serial.print("Secs to feeding:");
//      Serial.println(nextFeedingInSecs);
    } else {
//      Serial.println("Next feeding is today");
//      Serial.print(nextFeeding);
//      Serial.print(" - ");
//      Serial.print(currentTimeSecs);
      nextFeedingInSecs = nextFeeding - currentTimeSecs;
//      Serial.print(" = ");
//      Serial.println(nextFeedingInSecs);
    }
  }

//// temp
//  if (hasNextFeeding) {
//    Serial.print("Has next feeding: ");
//    Serial.print(nextFeeding);
//    Serial.print(", current: ");
//    Serial.println(currentTimeSecs);
//    
//    Serial.print("Feeding in: ");
//    Serial.print(nextFeedingInSecs);
//    Serial.println(" secs");
//  }

  switch (state)
  {
  case 1: // IDLE state
    handleIdleState();
    break;
  case 100: // Main menu
    handleMainMenu();
    break;
  case 200: // Time menu
    handleSetTimeMenu();
    break;
  case 300: // Feedings menu
    handleFeedingsMenu();
    break;
  case 310: // New Feeding
    handleNewFeedingMenu();
    break;
  case 350: // Delete feeding
    handleDeleteFeeding();
    break;
  default:
    setIdleState();
  }

  if (state != prevState)
  {
    Serial.println("Changed state");
    lcd.clear();
  }

  //Serial.println();

  if (!now.IsValid())
  {
    // Common Causes:
    //    1) the battery on the device is low or even missing and the power line was disconnected
    Serial.println("RTC lost confidence in the DateTime!");
    lcd.setCursor(0, 1);
    lcd.print("The battery on the device is low");
  }

  prevButtonSet = valSet;
  prevButtonPlus = valPlus;
  prevButtonMinus = valMinus;
  prevSwitch = valSwitch;

  prevState = state;
  lastLoop = millis();
}

void handleIdleState()
{
  printIDLEDisplay();
  if (!valSet && prevButtonSet)
  {
    setMainMenuState();
  }
}
void handleMainMenu()
{

  if (!valPlus && prevButtonPlus)
  {
    menuPage += 1;
    if (menuPage > 2)
    {
      menuPage = 0;
    }
    lcd.clear();
  }
  if (!valMinus && prevButtonMinus)
  {
    menuPage -= 1;
    if (menuPage < 0)
    {
      menuPage = 2;
    }
    lcd.clear();
  }

  lcd.setCursor(0, 0);
  lcd.print("Main menu");
  lcd.setCursor(0, 1);
  switch (menuPage)
  {
  case 0:
    lcd.print("Feedings");
    if (!valSet && prevButtonSet)
    {
      setFeedingsState();
    }
    break;
  case 1:
    lcd.print("Set time");
    if (!valSet && prevButtonSet)
    {
      setTimeMenuState();
    }
    break;
  case 2:
    lcd.print("Go back");
    if (!valSet && prevButtonSet)
    {
      setIdleState();
    }
    break;
  }
}
void handleSetTimeMenu()
{

  sinceLastBlink += timeSinceLastLoop;
  if (sinceLastBlink > blinkTime)
  {
    blinkState = !blinkState;
  }

  if (!valSet && prevButtonSet)
  {
    menuPage += 1;
    if (menuPage > 1)
    {
      setIdleState();
    }
    lcd.clear();
  }

  if (!valPlus && prevButtonPlus)
  {
    now += menuPage == 0 ? 3600L : 60L;
    Rtc.SetDateTime(now);
  }
  else if (!valMinus && prevButtonMinus)
  {
    now -= menuPage == 0 ? 3600L : 60L;
    Rtc.SetDateTime(now);
  }

  lcd.setCursor(0, 0);
  lcd.print("Set current time:");
  lcd.setCursor(0, 1);

  if (!blinkState || menuPage != 0)
  {
    if (now.Hour() < 10)
    {
      lcd.print("0");
    }
    lcd.print(now.Hour());
  }
  else
  {
    lcd.print("  ");
  }
  lcd.print(":");
  if (!blinkState || menuPage != 1)
  {
    if (now.Minute() < 10)
    {
      lcd.print("0");
    }
    lcd.print(now.Minute());
  }
  else
  {
    lcd.print("  ");
  }
}
void handleFeedingsMenu()
{
  if (!valPlus && prevButtonPlus)
  {
    menuPage += 1;
    if (menuPage > feedingCount + 1)
    {
      menuPage = 0;
    }
    lcd.clear();
  }
  if (!valMinus && prevButtonMinus)
  {
    menuPage -= 1;
    if (menuPage < 0)
    {
      menuPage = feedingCount + 1;
    }
    lcd.clear();
  }

  if (menuPage == feedingCount)
  {
    lcd.setCursor(0, 0);
    lcd.print("Feedings menu");
    lcd.setCursor(0, 1);
    lcd.print("Add new feeding");
    if (!valSet && prevButtonSet)
    {
      setNewFeedingState();
    }
  }
  else if (menuPage == feedingCount + 1)
  {
    lcd.setCursor(0, 0);
    lcd.print("Feedings menu");
    lcd.setCursor(0, 1);
    lcd.print("Go back");

    if (!valSet && prevButtonSet)
    {
      setIdleState();
    }
  }
  else
  {
    if (!valSet && prevButtonSet)
    {
      setDeleteFeedingState();
      return;
    }

    wHour = floor(feedings[menuPage].getTime() / 3600L);
    wMin = floor((feedings[menuPage].getTime() - wHour * 3600L) / 60L);

    lcd.setCursor(0, 0);
    lcd.print("Feeding: ");
    lcd.print(menuPage + 1);
    lcd.setCursor(0, 1);
    if (wHour < 10)
    {
      lcd.print("0");
    }
    lcd.print(wHour);
    lcd.print(":");
    if (wMin < 10)
    {
      lcd.print("0");
    }
    lcd.print(wMin);
    lcd.print(", P: ");
    lcd.print(feedings[menuPage].getPortions());
  }
}
void handleNewFeedingMenu()
{
  sinceLastBlink += timeSinceLastLoop;
  if (sinceLastBlink > blinkTime)
  {
    blinkState = !blinkState;
  }

  if (!valSet && prevButtonSet)
  {
    if (menuPage == 3)
    {
      if (!shouldSave)
      {
        setIdleState();
        return;
      }

      addFeeding(newFeedingTime, newFeedingPortions);
      setFeedingsState();
      return;
    }

    menuPage += 1;
    if (menuPage > 4)
    {
      menuPage = 0;
    }
  }

  if (!valPlus && prevButtonPlus)
  {
    if (menuPage == 0)
    {
      newFeedingTime += 3600;
    }
    else if (menuPage == 1)
    {
      newFeedingTime += 60;
    }
    else if (menuPage == 2)
    {
      newFeedingPortions += 1;
      if (newFeedingPortions > 15)
      {
        newFeedingPortions = 15;
      }
    }
    else if (menuPage == 3)
    {
      if (!shouldSave)
      {
        shouldSave = true;
        menuPage = 0;
      }
      else
      {
        shouldSave = false;
      }
    }

    if (newFeedingTime > 86400L)
    {
      newFeedingTime -= 86400L;
    }
  }
  if (!valMinus && prevButtonMinus)
  {
    if (menuPage == 0)
    {
      if (newFeedingTime < 3600L)
      {
        newFeedingTime = 86400L - 3600L - newFeedingTime;
      }
      else
      {
        newFeedingTime -= 3600L;
      }
    }
    else if (menuPage == 1)
    {
      if (newFeedingTime < 60)
      {
        newFeedingTime = 86400L - 60L - newFeedingTime;
      }
      else
      {
        newFeedingTime -= 60;
      }
    }
    else if (menuPage == 2)
    {
      if (newFeedingPortions != 1)
      {
        newFeedingPortions -= 1;
      }
    }
    else if (menuPage == 3)
    {
      if (!shouldSave)
      {
        shouldSave = true;
      }
      else
      {
        shouldSave = true;
        menuPage = 0;
      }
    }

    if (newFeedingTime < 0)
    {
      newFeedingTime += 86400L;
    }
  }

  switch (menuPage)
  {
  case 2:
    lcd.setCursor(0, 0);
    lcd.print("New feeding");
    lcd.setCursor(0, 1);
    lcd.print("Portions: ");
    if (!blinkState)
    {
      lcd.print(newFeedingPortions);
    }
    else
    {
      lcd.print("  ");
    }
    break;
  case 3:
    lcd.setCursor(0, 0);
    lcd.print("New feeding");
    lcd.setCursor(0, 1);
    lcd.print("Save? ");

    if (!blinkState && shouldSave || !shouldSave)
    {
      lcd.print("Yes");
    }
    else
    {
      lcd.print("   ");
    }
    lcd.print("/");
    if (!blinkState && !shouldSave || shouldSave)
    {
      lcd.print("No");
    }
    else
    {
      lcd.print("  ");
    }
    break;
  default:
    wHour = floor(newFeedingTime / 3600L);
    if (wHour == 24)
    {
      wHour = 0;
    }
    hourSecs = wHour * 3600;
    wMin = floor((newFeedingTime - hourSecs) / 60L);

    lcd.setCursor(0, 0);
    lcd.print("New feeding");
    lcd.setCursor(0, 1);
    lcd.print("Time: ");
    if (!blinkState || menuPage != 0)
    {
      if (wHour < 10)
      {
        lcd.print("0");
      }
      lcd.print(wHour);
    }
    else
    {
      lcd.print("  ");
    }
    lcd.print(":");
    if (!blinkState || menuPage != 1)
    {
      if (wMin < 10)
      {
        lcd.print("0");
      }
      lcd.print(wMin);
    }
    else
    {
      lcd.print("  ");
    }
    break;
  }
}
void handleDeleteFeeding()
{
  sinceLastBlink += timeSinceLastLoop;
  if (sinceLastBlink > blinkTime)
  {
    blinkState = !blinkState;
  }

  if (!valPlus && prevButtonPlus || !valMinus && prevButtonMinus) {
    shouldSave = !shouldSave;
  }

  if (!valSet && prevButtonSet) {
    deleteFeedingAt(menuPage);
    setFeedingsState();
    return;
  }

  wHour = floor(feedings[menuPage].getTime() / 3600L);
  if (wHour == 24)
  {
    wHour = 0;
  }
  hourSecs = wHour * 3600L;
  wMin = floor((feedings[menuPage].getTime() - hourSecs) / 60L);

  lcd.setCursor(0, 0);
  lcd.print("F: ");
  lcd.print(menuPage + 1);
  lcd.print(", ");
  if (wHour < 10) {
    lcd.print("0");
  }
  lcd.print(wHour);
  lcd.print(":");
  if (wMin < 10) {
    lcd.print("0");
  }
  lcd.print(wMin);

  lcd.setCursor(0, 1);
  lcd.print("Delete? ");

  if (!blinkState && shouldSave || !shouldSave)
  {
    lcd.print("Yes");
  }
  else
  {
    lcd.print("   ");
  }
  lcd.print("/");
  if (!blinkState && !shouldSave || shouldSave)
  {
    lcd.print("No");
  }
  else
  {
    lcd.print("  ");
  }
}

void setMainMenuState()
{
  Serial.println("Changing to Main Menu State");
  state = 100;
  menuPage = 0;
}
void setIdleState()
{
  Serial.println("Changing to Idle State");
  state = 1; 
  // TODO: This is a hack, why is it not finding otherwise?
  hasNextFeeding = false;
}
void setTimeMenuState()
{
  Serial.println("Changing to Time menu State");
  state = 200;
  menuPage = 0;
}
void setFeedingsState()
{
  Serial.println("Changing to Feeding Menu State");
  state = 300;
  menuPage = 0;
  hasNextFeeding = false;
}
void setNewFeedingState()
{
  Serial.println("Changing to New Feeding Menu State");
  state = 310;
  newFeedingTime = 43200L; // 12 PM
  newFeedingPortions = 1;
  menuPage = 0;
  shouldSave = true;
}
void setDeleteFeedingState()
{
  Serial.println("Changing to Delete Feeding Menu State");
  state = 350;
  shouldSave = true;
}

void addFeeding(const unsigned long feedingTime, const int portions)
{
  Serial.print("Adding feeding: ");
  Serial.print(feedingTime);
  Serial.print(", p: ");
  Serial.println(portions);

  int nextIndex = feedingCount;
  feedings[nextIndex] = FeedingData(feedingTime, portions);
  feedingCount += 1;
  sortFeedings();
  saveDataToMemory();
  hasNextFeeding = false;
}
void deleteFeedingAt(const int feedingIndex) {
  for (int i = feedingIndex + 1; i < feedingCount; i++) {
    feedings[i - 1] = feedings[i];
  }
  feedingCount -= 1;
  saveDataToMemory();
  hasNextFeeding = false;
}

void sortFeedings()
{
  qsort(feedings, feedingCount, sizeof(feedings[0]), sortFeedings);
}
int sortFeedings(const void *cmp1, const void *cmp2)
{
  // Need to cast the void * to int *
  FeedingData a = *((FeedingData *)cmp1);
  FeedingData b = *((FeedingData *)cmp2);
  // The comparison
  return a.getTime() < b.getTime() ? -1 : (a.getTime() > b.getTime() ? 1 : 0);
  // A simpler, probably faster way:
  //return b - a;
}

void loadDataFromStorage() {
  EEPROM.get(0, feedingCount);
  if (feedingCount < 1) {
    feedingCount = 0;
    return;
  }
  
  int eeAddress = sizeof(feedingCount);
  int currentIndex = 0;
  for (int i = 0; i < feedingCount; i++) {
      EEPROM.get(eeAddress, newFeedingTime);
      eeAddress += sizeof(newFeedingTime);
      EEPROM.get(eeAddress, newFeedingPortions);
      eeAddress += sizeof(newFeedingPortions);

      feedings[currentIndex] = FeedingData(newFeedingTime, newFeedingPortions);
      currentIndex++;
  }

  Serial.println("Memory loaded:");
  dumpMemory();
}

void saveDataToMemory() {
  Serial.println("Writing memory to storage");
  EEPROM.put(0, feedingCount);
  int eeAddress = sizeof(feedingCount);
  for (int i = 0; i < feedingCount; i++) {
      EEPROM.put(eeAddress, feedings[i].getTime());
      eeAddress += sizeof(newFeedingTime);
      EEPROM.put(eeAddress, feedings[i].getPortions());
      eeAddress += sizeof(newFeedingPortions);
  }
  Serial.println("Done");
}

void dumpMemory()
{
  Serial.println("Dump memory:");
  Serial.print("Array Size: ");
  Serial.println(feedingCount);
  for (int i = 0; i < feedingCount; i++)
  {
    Serial.print("[");
    Serial.print(i);
    Serial.print("] = { time: ");
    Serial.print(feedings[i].getTime());
    Serial.print(", portions: ");
    Serial.print(feedings[i].getPortions());
    Serial.println(" }");
  }
  Serial.println("-------------------");
}

void feed(const int portions) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("FEEDING!");

  for (int i = 1; i <= portions; i++) {
    lcd.setCursor(0, 1);
    lcd.print("Portion: ");
    lcd.print(i);
    lcd.print(" of ");
    lcd.print(portions);

    digitalWrite(engineOut, HIGH);
    if(digitalRead(switchButton) == HIGH) {
      Serial.println("Waiting for switch to turn low");
      while(digitalRead(switchButton) == HIGH) {
        
//        Serial.print("W LOW: S:");
//        Serial.print(valSet);
//        Serial.print(", P:");
//        Serial.print(valPlus);
//        Serial.print(", M:");
//        Serial.print(valMinus);
//        Serial.print(", S:");
//        Serial.println(digitalRead(switchButton) == HIGH);
        //Serial.println("Waiting for switch button to go low");
      }
      Serial.println("Switch is now low");
    }
    
    Serial.println("Waiting for switch to turn high");
    while(digitalRead(switchButton) != HIGH) {
//        Serial.print("W HIGH: S:");
//        Serial.print(valSet);
//        Serial.print(", P:");
//        Serial.print(valPlus);
//        Serial.print(", M:");
//        Serial.print(valMinus);
//        Serial.print(", S:");
//        Serial.println(digitalRead(switchButton) == HIGH);
        //Serial.println("Waiting for switch button to go high");
    }
    Serial.println("Switch is now high");
  }

  digitalWrite(engineOut, LOW);
  lcd.clear();
}

void printIDLEDisplay()
{
  lcd.setCursor(0, 0);
  lcd.print("Time: ");
  if (now.Hour() < 10) {
    lcd.print("0");
  }
  lcd.print(now.Hour());
  lcd.print(":");
  if (now.Minute() < 10) {
    lcd.print("0");
  }
  lcd.print(now.Minute());
  lcd.print(":");
  if (now.Second() < 10) {
    lcd.print("0");
  }
  lcd.print(now.Second());
  lcd.setCursor(0, 1);
  
  lcd.print("Next: ");
  wHour = floor(nextFeedingInSecs / 3600);
  wMin = floor((nextFeedingInSecs - wHour * 3600) / 60);
  if (wHour < 10) {
    lcd.print("0");
  }
  lcd.print(wHour);
  lcd.print(":");
  if (wMin < 10) {
    lcd.print("0");
  }
  lcd.print(wMin);
  lcd.print(":");
  if ((nextFeedingInSecs - (wHour * 3600) - (wMin * 60)) < 10) {
    lcd.print("0");
  }
  lcd.print((nextFeedingInSecs - (wHour * 3600) - (wMin * 60)));
  
}

void calculateButtonState()
{
//  Serial.print("State: S:");
//  Serial.print(valSet);
//  Serial.print(", P:");
//  Serial.print(valPlus);
//  Serial.print(", M:");
//  Serial.print(valMinus);
//  Serial.print(", S:");
//  Serial.println(digitalRead(switchButton) == HIGH);

  
  if (valSet == HIGH)
  {
    holdMsButtonSet += timeSinceLastLoop;
    isHoldingSet = holdMsButtonSet >= holdTimsMs;
  }
  else
  {
    holdMsButtonSet = 0;
    isHoldingSet = false;
  }

  if (valPlus == HIGH)
  {
    holdMsButtonPlus += timeSinceLastLoop;
    isHoldingPlus = holdMsButtonPlus >= holdTimsMs;
  }
  else
  {
    holdMsButtonPlus = 0;
    isHoldingPlus = false;
  }

  if (valMinus == HIGH)
  {
    holdMsButtonMinus += timeSinceLastLoop;
    isHoldingMinus = holdMsButtonMinus >= holdTimsMs;
  }
  else
  {
    holdMsButtonMinus = 0;
    isHoldingMinus = false;
  }
}

void setupRTC()
{
  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);

  if (!Rtc.IsDateTimeValid())
  {
    // Common Causes:
    //    1) first time you ran and the device wasn't running yet
    //    2) the battery on the device is low or even missing

    Serial.println("RTC lost confidence in the DateTime!");
    Rtc.SetDateTime(compiled);
  }

  if (Rtc.GetIsWriteProtected())
  {
    Serial.println("RTC was write protected, enabling writing now");
    Rtc.SetIsWriteProtected(false);
  }

  if (!Rtc.GetIsRunning())
  {
    Serial.println("RTC was not actively running, starting now");
    Rtc.SetIsRunning(true);
  }

  RtcDateTime now = Rtc.GetDateTime();
  if (now < compiled)
  {
    Serial.println("RTC is older than compile time!  (Updating DateTime)");
    Rtc.SetDateTime(compiled);
  }
  else if (now > compiled)
  {
    Serial.println("RTC is newer than compile time. (this is expected)");
  }
  else if (now == compiled)
  {
    Serial.println("RTC is the same as compile time! (not expected but all is fine)");
  }
}
