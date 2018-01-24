#if !defined(Defines_h)
#define Defines_h

//  PWM values are between 1000 and 2000, with 1500 meaning "zero"

//  Limit the throttle, so that kiddos don't go too crazy.
#define MIN_THROTTLE 1000
#define MAX_THROTTLE 2000
#define THROTTLE_CENTER 1500
#define THROTTLE_ADJUSTMENT 0

//  Slightly limit steering to prevent the servo from bottoming out.
#define MIN_STEER 1300
#define MAX_STEER 1700
#define STEER_CENTER 1500
//  My steering is slightly off to the left, so compensate.
#define STEER_ADJUSTMENT 0
//  My HiTec servo is backwards. Set this to 1 for regular.
#define STEER_MULTIPLY -1
//area around center that we count as center
#define VAL_DRIFT 2

#define PIN_STEER_IN 11
#define PIN_THROTTLE_IN 12
#define PIN_MODE_IN 13
#define PIN_A_VOLTAGE_IN A2

#define PIN_STEER_OUT 5
#define PIN_THROTTLE_OUT 6
#define PIN_POWER_CONTROL 20

#define RPI_SERIAL Serial3  //  UART
//#define RPI_SERIAL Serial   //  USB
#define RPI_BAUD_RATE 115200

#define IBUS_SERIAL Serial1

//  After the last valid input, wait this long before deciding 
//  that no more inputs will come. (milliseconds)
#define INPUT_TIMEOUT 500

//  How many subsequent readings below the bad voltage before turning off.
//  Count up to avoid temporary spikes shutting everything down. Each reading
//  is done every 100 milliseconds.
#define NUM_BAD_VOLT_TO_TURN_OFF 20
//  ADC returns 4095 at 3.3V; voltage divider is 5.7:1, so 18.8V at 4095
//  You may need to tweak this value based on the reference in your chip.
#define VOLTAGE_FACTOR 0.0046f
//  how low to let the cells go before turning off
#define VOLTAGE_PER_CELL 3.2f

//  If POWER_DEVMODE is on, we trust the weak pull-up to keep the 
//  power FET on, and don't configure power-control as output unless
//  we want it to explicitly turn off. This allows reliable re-programming
//  of the Teensy in place. With POWER_DEVMODE off, the Teensy will pull 
//  the power FET on or off using digital output.
#define POWER_DEVMODE 1

#endif  //  Defines_h

