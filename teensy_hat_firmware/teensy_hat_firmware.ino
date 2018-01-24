#include "Defines.h"
#include "PinPulseIn.h"
#include "FlySkyIBus.h"
#include "PCA9685Emulator.h"

#include <Servo.h>


PinPulseIn<11> rcSteer;
PinPulseIn<12> rcThrottle;
PinPulseIn<13> rcMode;

Servo svoSteer;
Servo svoThrottle;

uint16_t iBusInput[10];
FlySkyIBus fsIbus(IBUS_SERIAL, iBusInput, 10);

PCA9685Emulator pwmEmulation;

uint16_t numBadVolt = 0;
float minimumVoltage = 6.0f;
float determinedVoltage = 0.0f;
uint16_t determinedCount = 0;

// C + CRLF + 0 == 4
// space + age <= 6
// space + value <= 6 each times 10 channels
char writeBuf[6*10+4+6];
char readBuf[64];
uint8_t readBufPtr;
uint16_t serialSteer;
uint16_t serialThrottle;

bool serialEchoEnabled = false;


void read_voltage(uint16_t &v, float &voltage) {
  v = analogRead(PIN_A_VOLTAGE_IN);
  voltage = VOLTAGE_FACTOR * v;
}

void setup() {
  analogReference(DEFAULT);
  analogReadRes(12);
  
  pinMode(PIN_STEER_IN, INPUT_PULLDOWN);
  pinMode(PIN_THROTTLE_IN, INPUT_PULLDOWN);
  pinMode(PIN_MODE_IN, INPUT_PULLDOWN);
  (void)analogRead(PIN_A_VOLTAGE_IN);

  pinMode(PIN_STEER_OUT, OUTPUT);
  svoSteer.attach(PIN_STEER_OUT);
  pinMode(PIN_THROTTLE_OUT, OUTPUT);
  svoThrottle.attach(PIN_THROTTLE_OUT);
#if defined(POWER_DEVMODE) && POWER_DEVMODE
  pinMode(PIN_POWER_CONTROL, INPUT_PULLUP);
#else
  pinMode(PIN_POWER_CONTROL, OUTPUT);
  digitalWrite(PIN_POWER_CONTROL, HIGH);
#endif

  rcSteer.begin();
  rcThrottle.begin();
  rcMode.begin();
  fsIbus.begin();

  RPI_SERIAL.begin(RPI_BAUD_RATE);

  Serial.begin(9600);
   
  pwmEmulation.begin(PCA9685_I2C_ADDRESS);

  //  allow things to settle
  delay(300);
}

uint32_t lastVoltageCheck = 0;

void check_voltage(uint32_t now) {
  /* Turn off if under-volt */
  uint16_t v;
  float voltage;
  if (now-lastVoltageCheck > 100) {
    read_voltage(v, voltage);
    v = (uint16_t)(voltage * 100 + 0.5f);
    if (determinedCount != 20) {
      determinedVoltage += voltage / 20;
      ++determinedCount;
      if (determinedCount == 20) {
        if (determinedVoltage > 3 * 4.3f) {
          minimumVoltage = VOLTAGE_PER_CELL * 4;
        } else if (determinedVoltage > 2 * 4.3f) {
          minimumVoltage = VOLTAGE_PER_CELL * 3;
        } else {
          minimumVoltage = VOLTAGE_PER_CELL * 2;
        }
      }
    }
    if (serialEchoEnabled && !!RPI_SERIAL) {
      char *wptr = writeBuf;
      *wptr++ = 'V';
      *wptr++ = ' ';
      itoa((int)v, wptr, 10);
      wptr += strlen(wptr);
      *wptr++ = ' ';
      itoa((int)(minimumVoltage * 100 + 0.5f), wptr, 10);
      wptr += strlen(wptr);
      *wptr++ = ' ';
      itoa((int)(determinedVoltage * 100 + 0.5f), wptr, 10);
      wptr += strlen(wptr);
      *wptr = 0;
      RPI_SERIAL.println(writeBuf);
    }
    lastVoltageCheck = now;
    if (voltage < minimumVoltage) {
      ++numBadVolt;
      if (numBadVolt > NUM_BAD_VOLT_TO_TURN_OFF) {
#if defined(POWER_DEVMODE) && POWER_DEVMODE
        pinMode(PIN_POWER_CONTROL, OUTPUT);
#endif
        digitalWrite(PIN_POWER_CONTROL, LOW);
        numBadVolt = NUM_BAD_VOLT_TO_TURN_OFF;
      }
    } else {
      if (numBadVolt > 0) {
        if (numBadVolt >= NUM_BAD_VOLT_TO_TURN_OFF) {
          //  I previously pulled power low, but am still running, so 
          //  presumably I'm being fed power through the non-switched connector.
          //  Thus, just reset things to "it's good now."
#if defined(POWER_DEVMODE) && POWER_DEVMODE
          pinMode(PIN_POWER_CONTROL, INPUT_PULLUP);
#else
          digitalWrite(PIN_POWER_CONTROL, HIGH);
#endif
        }
        --numBadVolt;
      }
    }
  }
}

uint32_t lastInputTime = 0;
uint16_t val = 0;

void read_inputs(uint32_t now) {
  
  /* update PWM inputs */
  if (rcSteer.hasValue()) {
    val = rcSteer.getValue();
    if(abs(iBusInput[0] - val) > VAL_DRIFT)
    {
      iBusInput[0] = val;
      lastInputTime = now;
    }
  }
  
  if (rcThrottle.hasValue()) {
    val = rcThrottle.getValue();
    if(abs(iBusInput[1] - val) > VAL_DRIFT)
    {
      iBusInput[1] = val;
      lastInputTime = now;
    }
  }
  
  if (rcMode.hasValue()) {
    val = rcMode.getValue();
    if(abs(iBusInput[2] - val) > VAL_DRIFT)
    {
      iBusInput[2] = val;
      lastInputTime = now;
    }
  }
  
  /* update ibus input */
  /*
  fsIbus.step(now);
  if (fsIbus.hasFreshFrame()) {
    lastInputTime = now;
  }
  */
  

  /*  RC and iBus will fight, if they are both wired up.  
   *  If only one is wired, that one will take over.
   *  Either don't do that, or live with the fighting -- the 
   *  delta will be small as they are the same value (as long
   *  as they're connected in the same order to the same 
   *  receiver)
   */
}

uint32_t lastI2cTime = 0;

void read_i2c(uint32_t now) {
  if (pwmEmulation.step(now)) {
    lastI2cTime = now;
  }
}

uint32_t lastSerialTime = 0;
int servalues[4] = { 0 };

void do_serial_command(char const *cmd, uint32_t now) {
  char cmdchar = *cmd;
  ++cmd;
  int nvals = 0;
  while (nvals < 4 && *cmd) {
    if (*cmd <= 32) {
      ++cmd;
    } else {
      servalues[nvals] = atoi(cmd);
      ++nvals;
      while (*cmd >= '0' && *cmd <= '9') {
        ++cmd;
      }
    }
  }
  
  Serial.println("doing serial command");
  
  if (cmdchar == 'D' && nvals == 2) {
    //  drive
    serialSteer = servalues[0];
    serialThrottle = servalues[1];
    lastSerialTime = now;
  } else if (*cmd == 'O' && nvals == 0) {
    //  off
    pinMode(PIN_POWER_CONTROL, OUTPUT);
    digitalWrite(PIN_POWER_CONTROL, LOW);
  } else if (*cmd == 'X' && nvals == 1) {
    //  enable/disable serial echo
    serialEchoEnabled = servalues[0] > 0;
  } else {
    //  unknown
    if (serialEchoEnabled) {
      RPI_SERIAL.println("?");
    }
  }
}

/*  Serial input commands are a command letter, followed by optional
 *  space separated arguments, followed by <CR><LF>.
 *  D steer throttle
 *  - D means "drive"
 *  - steer is 1000 to 2000
 *  - throttle is 1000 to 2000
 *  O
 *  - O means "turn off"
 */
void read_serial(uint32_t now) {
  while (RPI_SERIAL.available() > 0) {
    if (readBufPtr == sizeof(readBuf)) {
      readBufPtr = 0;
    }
    readBuf[readBufPtr++] = RPI_SERIAL.read();
    if (readBufPtr >= 2 && readBuf[readBufPtr-2] == 13 && readBuf[readBufPtr-1] == 10) {
      readBuf[readBufPtr-2] = 0;
      do_serial_command(readBuf, now);
      readBufPtr = 0;
    }
  }
}

void apply_control_adjustments(uint16_t &steer, uint16_t &throttle) {
  //steer = (uint16_t)(((int)steer - STEER_CENTER + STEER_ADJUSTMENT) * STEER_MULTIPLY + STEER_CENTER);
  if(steer == 0)
    steer = STEER_CENTER;
    
  if (steer < MIN_STEER) {
    steer = MIN_STEER;
  }
  if (steer > MAX_STEER) {
    steer = MAX_STEER;
  }

  throttle += THROTTLE_ADJUSTMENT;
  if (throttle < MIN_THROTTLE) {
    throttle = MIN_THROTTLE;
  }
  if (throttle > MAX_THROTTLE) {
    throttle = MAX_THROTTLE;
  }
}

uint16_t last_steer = STEER_CENTER;

uint16_t last_i2c_steer = 0;
uint16_t last_i2c_th = 0;

void filter_i2c_events()
{
  uint16_t steer = STEER_CENTER;
  uint16_t throttle = THROTTLE_CENTER;
  
  if (lastI2cTime) 
  {
    steer = pwmEmulation.readChannelUs(0);
    throttle = pwmEmulation.readChannelUs(1);

    if (steer == last_i2c_steer && throttle == last_i2c_th && abs(steer - STEER_CENTER) < 35) 
    {
        lastI2cTime = 0;
    }
    
    last_i2c_steer = steer;
    last_i2c_th = throttle;
  }
}
  
void generate_output(uint32_t now) {

  uint16_t steer = STEER_CENTER;
  uint16_t throttle = THROTTLE_CENTER;
  bool autoSource = false;

  filter_i2c_events();

  if (lastInputTime && ((iBusInput[2] > 1800) || (iBusInput[9] > 1800))) {
    //  if aux channel is high, or tenth channel (right switch on FSi6) is set,
    //  just drive, without auto.
    steer = iBusInput[0];
    throttle = iBusInput[1];
  }
  else if (lastI2cTime) {
    //  I2C trumps serial
    steer = pwmEmulation.readChannelUs(0);
    throttle = pwmEmulation.readChannelUs(1);
    autoSource = true;
  } else if (lastSerialTime) {
    //  serial trumps manual
    steer = serialSteer;
    throttle = serialThrottle;
    autoSource = true;
  } else if (lastInputTime) {
    //  manual drive if available
    steer = iBusInput[0];
    throttle = iBusInput[1];
  }

  /*
  if (autoSource) {
    //  safety control if auto-driving
    if (iBusInput[1] < 360) {
      //  back up if throttle control says so
      throttle = min(iBusInput[1], throttle);
      steer = 370;
    } else {
      if (iBusInput[1] < 1600) {
        //  don't allow driving if throttle doesn't say drive
        throttle = 1500;
      }
    }
  }
  */
  
  apply_control_adjustments(steer, throttle);
  
  if(steer != last_steer)
  {
    Serial.print("s:");
    Serial.print(steer);
    //Serial.print("t:");
    //Serial.print(throttle);
    Serial.println(" ");
    last_steer = steer;
  }
  
  //steer = 1500;
  //throttle = 1500;
   
  svoSteer.writeMicroseconds(steer);
  svoThrottle.writeMicroseconds(throttle);
}

uint32_t lastSerialWrite = 0;

void update_serial(uint32_t now) {
  if (now - lastSerialWrite > 32) {
    lastSerialWrite = now;
    if (serialEchoEnabled && !!RPI_SERIAL) {
      char *wbuf = writeBuf;
      int d = (int)(now - lastInputTime);
      if (d > 1000 || d < 0) {
        strcpy(wbuf, "C -1");
      } else {
        *wbuf++ = 'C';
        *wbuf++ = ' ';
        itoa(d, wbuf, 10);
      }
      wbuf += strlen(wbuf);
      for (int i = 0; i != 10; ++i) {
        *wbuf++ = ' ';
        itoa(iBusInput[i], wbuf, 10);
        wbuf += strlen(wbuf);
      }
      *wbuf = 0;
      RPI_SERIAL.println(writeBuf);
    }
  }
}

void loop() {
  
  uint32_t now = millis();
  while (now == 0) {
    delay(1);
    now = millis();
  }

  read_inputs(now);
  if (lastInputTime && (now - lastInputTime > INPUT_TIMEOUT)) {
    //  Decide that we don't have any control input if there 
    //  is nothing coming in for a while.
      lastInputTime = 0;
    }
  
  /* look for i2c drive commands */
  read_i2c(now);
  if (lastI2cTime && (now - lastI2cTime > INPUT_TIMEOUT)) {
    lastI2cTime = 0;
  }

/*
  read_serial(now);
  if (lastSerialTime && (now - lastSerialTime > INPUT_TIMEOUT)) {
    lastSerialTime = 0;
  }
  */
  
  generate_output(now);

  //update_serial(now);

  check_voltage(now);
}


