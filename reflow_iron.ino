#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Adafruit_MAX31855.h"

#define MAX_ULONG 4294967295

#define COOLDOWN_TIME 60000
#define PREHEAT_TIME 60000
#define REFLOW_TIME 60000

const int display_addr = 0x3C;
#define OLED_RESET 0  // GPIO0
Adafruit_SSD1306 display(OLED_RESET);

int thermoDO = 10;
int thermoCS = 11;
int thermoCLK = 12;

Adafruit_MAX31855 thermocouple(thermoCLK, thermoCS, thermoDO);

#define BUTTON_PIN 2
#define ENCODER_A_PIN 3
#define ENCODER_B_PIN 4
#define RELAY_PIN 15

#define BOUNCE_DELAY 5
#define BUTTON_LONG_PRESS 3000
#define BUTTON_REPRESS_DELAY 200
#define DISPLAY_FLASH_DELAY 200
#define ENCODER_LED_FLASH 250

const String app_version = "Reflow v0.1";

#define MIN_PREHEAT_TEMP 100
#define MAX_PREHEAT_TEMP 180
#define MIN_PREHEAT_TIME 0
#define MAX_PREHEAT_TIME 180000
#define MIN_REFLOW_TEMP 160
#define MAX_REFLOW_TEMP 250
#define MIN_REFLOW_TIME 1000
#define MAX_REFLOW_TIME 180000

int temp_preheat = 150;
unsigned long time_preheat = PREHEAT_TIME;
int temp_reflow = 220;
unsigned long time_reflow = REFLOW_TIME;

int temp_now = 0;
int temp_set = 0;

enum IRON_STATE {
  OFF,
  CONFIG_PREHEAT_TEMP,
  CONFIG_PREHEAT_TEMP_SET,
  CONFIG_PREHEAT_TIME,
  CONFIG_PREHEAT_TIME_SET,
  CONFIG_REFLOW_TEMP,
  CONFIG_REFLOW_TEMP_SET,
  CONFIG_REFLOW_TIME,
  CONFIG_REFLOW_TIME_SET,
  CONFIG_START,
  PREHEAT,
  REFLOW,
  COOLING
} state;

IRON_STATE config_state_roll[] = {
  CONFIG_PREHEAT_TIME,
  CONFIG_REFLOW_TEMP,
  CONFIG_REFLOW_TIME,
  CONFIG_START,
  CONFIG_PREHEAT_TEMP,
  CONFIG_PREHEAT_TIME,
  CONFIG_REFLOW_TEMP,
  CONFIG_REFLOW_TIME,
  CONFIG_START,
};

volatile int button = 0;
volatile unsigned long t_button = 0;

volatile int encoder_a = 0;
volatile int encoder_b = 0;
volatile int encoder_count = 0;
volatile unsigned long t_encoder = 0;

int seconds = 0;
int percent = 0;

int offset = 0;

void pciSetup(byte pin)
{
  *digitalPinToPCMSK(pin) |= bit (digitalPinToPCMSKbit(pin));  // enable pin
  PCIFR |= bit(digitalPinToPCICRbit(pin)); // clear any outstanding interrupt
  PCICR |= bit(digitalPinToPCICRbit(pin)); // enable interrupt for the group
}

// Use one Routine to handle each group
 
ISR (PCINT0_vect)
{
  // handle pin change interrupt for D8 to D13 here
}
 
ISR (PCINT1_vect)
{
  // handle pin change interrupt for A0 to A5 here
}  
 
ISR (PCINT2_vect)
{
  // handle pin change interrupt for D0 to D7 here
  
  int btn = digitalRead(BUTTON_PIN);
  unsigned long t = millis();
  if (btn != button && t >= t_button + BOUNCE_DELAY)
  {
    button = btn;
    t_button = t;
  }

  int a = digitalRead(ENCODER_A_PIN);
  int b = digitalRead(ENCODER_B_PIN);
  t = millis();
  if (a != encoder_a && t >= t_encoder + BOUNCE_DELAY)
  {
    encoder_a = a;
    t_encoder = t;

    if (a == LOW)
    {
      if (b == LOW)
      {
        // CCW
        encoder_count--;
      }
      else
      {
        // CW
        encoder_count++;
      }
    }
    else if (b == LOW)
    {
      // CW
      encoder_count++;
    }
    else
    {
      // CCW
      encoder_count--;
    }
  }
  else if (b != encoder_b && t >= t_encoder + BOUNCE_DELAY)
  {
    encoder_b = b;
    t_encoder = t;

    if (b == LOW)
    {
      if (a == LOW)
      {
        // CW
        encoder_count++;
      }
      else
      {
        // CCW
        encoder_count--;
      }
    }
    else if (a == LOW)
    {
      // CCW
      encoder_count--;
    }
    else
    {
      // CW
      encoder_count++;
    }
  }
}

void setup()
{
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(ENCODER_A_PIN, INPUT_PULLUP);
  pinMode(ENCODER_B_PIN, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  button = digitalRead(BUTTON_PIN);
  encoder_a = digitalRead(ENCODER_A_PIN);
  encoder_b = digitalRead(ENCODER_B_PIN);

  // enable interrupt for pin...
  pciSetup(BUTTON_PIN);
  pciSetup(ENCODER_A_PIN);
  pciSetup(ENCODER_B_PIN);

  display.begin(SSD1306_SWITCHCAPVCC, display_addr);
  display.clearDisplay();
  display.display();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(app_version);

  Serial.begin(9600);
  Serial.println(app_version);

  state = CONFIG_START;
  
  delay(500);
}

unsigned long t_display = millis();
unsigned long t_state = 0;
unsigned long t_hold = 0;
unsigned long t_now = 0;
unsigned long t_display_flash = 0;

int prev_encoder_count = 0;
int prev_button = 0;
unsigned long prev_t_encoder = 0;
IRON_STATE prev_state = OFF;

void loop()
{
  temp_now = thermocouple.readCelsius();
  t_now = millis();

  if (state != prev_state || encoder_count != prev_encoder_count || button != prev_button || t_encoder != prev_t_encoder)
  {
    Serial.println(String(t_now) + " " + state_name(state) + " " + String(temp_now) + "°C/"+ String(temp_set) + "°C H" + String(t_hold) + " P" + String(button) + " A" + String(encoder_a) + " B" + String(encoder_b) + " C" + String(encoder_count) + " T" + String(t_encoder));
    prev_encoder_count = encoder_count;
    prev_button = button;
    prev_t_encoder = t_encoder;
    prev_state = state;
  }
  
  switch (state) {
    case OFF:
      if (button == LOW)
      {
        if (t_now >= t_button + BUTTON_LONG_PRESS)
        {
          state = PREHEAT;
          temp_set = temp_preheat;
        }
        else
        {
          state = CONFIG_START;
        }
        
        t_state = t_now;
        t_hold = 0;
        reset_button();
      }
      else if (t_now >= t_state + 2000)
      {
        display.clearDisplay();

        t_state = MAX_ULONG - 2000;
      }
      break;

    case CONFIG_PREHEAT_TEMP:
      flash_encoder_led();
      
      if (button == LOW)
      {
        state = CONFIG_PREHEAT_TEMP_SET;
        t_state = t_now;
        t_display_flash = t_now;
        reset_button();
      }
      else if (encoder_count != 0)
      {
        state = next_config_state(state, encoder_count);
        encoder_count = 0;
      }
      break;

    case CONFIG_PREHEAT_TEMP_SET:
      flash_encoder_led();

      if (button == LOW)
      {
        state = CONFIG_PREHEAT_TEMP;
        t_state = t_now;
        t_display_flash = 0;
        reset_button();
      }
      else if (encoder_count != 0)
      {
        temp_preheat += encoder_count;
        encoder_count = 0;

        if (temp_preheat < MIN_PREHEAT_TEMP)
        {
          temp_preheat = MIN_PREHEAT_TEMP;
        }
        else if (temp_preheat > MAX_PREHEAT_TEMP)
        {
          temp_preheat = MAX_PREHEAT_TEMP;
        }
      }
      break;

    case CONFIG_PREHEAT_TIME:
      flash_encoder_led();

      if (button == LOW)
      {
        state = CONFIG_PREHEAT_TIME_SET;
        t_state = t_now;
        t_display_flash = t_now;
        reset_button();
      }
      else if (encoder_count != 0)
      {
        state = next_config_state(state, encoder_count);
        encoder_count = 0;
      }
      break;

    case CONFIG_PREHEAT_TIME_SET:
      flash_encoder_led();

      if (button == LOW)
      {
        state = CONFIG_PREHEAT_TIME;
        t_state = t_now;
        t_display_flash = 0;
        reset_button();
      }
      else if (encoder_count != 0)
      {
        time_preheat += encoder_count * 1000;
        encoder_count = 0;

        if (time_preheat < MIN_PREHEAT_TIME)
        {
          time_preheat = MIN_PREHEAT_TIME;
        }
        else if (time_preheat > MAX_PREHEAT_TIME)
        {
          time_preheat = MAX_PREHEAT_TIME;
        }
      }
      break;

    case CONFIG_REFLOW_TEMP:
      flash_encoder_led();

      if (button == LOW)
      {
        state = CONFIG_REFLOW_TEMP_SET;
        t_state = t_now;
        t_display_flash = t_now;
        reset_button();
      }
      else if (encoder_count != 0)
      {
        state = next_config_state(state, encoder_count);
        encoder_count = 0;
      }
      break;

    case CONFIG_REFLOW_TEMP_SET:
      flash_encoder_led();

      if (button == LOW)
      {
        state = CONFIG_REFLOW_TEMP;
        t_state = t_now;
        t_display_flash = 0;
        reset_button();
      }
      else if (encoder_count != 0)
      {
        temp_reflow += encoder_count;
        encoder_count = 0;

        if (temp_reflow < MIN_REFLOW_TEMP)
        {
          temp_reflow = MIN_REFLOW_TEMP;
        }
        else if (temp_reflow > MAX_REFLOW_TEMP)
        {
          temp_reflow = MAX_REFLOW_TEMP;
        }
      }
      break;

    case CONFIG_REFLOW_TIME:
      flash_encoder_led();

      if (button == LOW)
      {
        state = CONFIG_REFLOW_TIME_SET;
        t_state = t_now;
        t_display_flash = t_now;
        reset_button();
      }
      else if (encoder_count != 0)
      {
        state = next_config_state(state, encoder_count);
        encoder_count = 0;
      }
      break;

    case CONFIG_REFLOW_TIME_SET:
      flash_encoder_led();

      if (button == LOW)
      {
        state = CONFIG_REFLOW_TIME;
        t_state = t_now;
        t_display_flash = 0;
        reset_button();
      }
      else if (encoder_count != 0)
      {
        time_reflow += encoder_count * 1000;
        encoder_count = 0;

        if (time_reflow < MIN_REFLOW_TIME)
        {
          time_reflow = MIN_REFLOW_TIME;
        }
        else if (time_reflow > MAX_REFLOW_TIME)
        {
          time_reflow = MAX_REFLOW_TIME;
        }
      }
      break;
      
    case CONFIG_START:
      if (button == LOW)
      {
        state = PREHEAT;
        temp_set = temp_preheat;
        t_state = t_now;
        reset_button();
      }
      else if (encoder_count != 0)
      {
        state = next_config_state(state, encoder_count);
        encoder_count = 0;
      }
      break;

    case PREHEAT:
      if (button == LOW && t_now >= t_button + BUTTON_LONG_PRESS)
      {
        // Abort on long press
        state = COOLING;
        temp_set = 0;
        t_state = t_now;
        t_hold = 0;
      }
      else
      {
        control_iron(temp_now, temp_set);
  
        if (temp_now >= temp_set && t_hold == 0)
        {
          t_hold = t_now + PREHEAT_TIME;
        }
        else if (t_hold > 0)
        {
          seconds = t_hold - t_now / 1000;
  
          if (t_now >= t_hold)
          {
            state = REFLOW;
            temp_set = temp_reflow;
            t_state = t_now;
            t_hold = 0;
          }
        }
      }
      break;

    case REFLOW:
      if (button == LOW && t_now >= t_button + BUTTON_LONG_PRESS)
      {
        // Abort on long press
        state = COOLING;
        temp_set = 0;
        t_state = t_now;
        t_hold = 0;
      }
      else
      {
        control_iron(temp_now, temp_set);
  
        if (temp_now >= temp_set && t_hold == 0)
        {
          t_hold = t_now + REFLOW_TIME;
        }
        else if (t_hold > 0)
        {
          seconds = t_hold - t_now / 1000;
  
          if (t_now >= t_hold)
          {
            state = COOLING;
            temp_set = 0;
            t_state = t_now;
            t_hold = 0;
          }
        }
      }
      break;

    case COOLING:
      control_iron(temp_now, temp_set);

      if (t_hold == 0)
      {
        t_hold = t_now + COOLDOWN_TIME;
      }
      else if (t_now >= t_hold)
      {
        state = OFF;
      }
      break;
  }

  if (t_hold > 0)
  {
    seconds = t_hold - t_now / 1000;
  }
  if (temp_set > 0)
  {
    percent = int((float(temp_now) / float(temp_set)) * 100);
  }

  t_now = millis();
  if (t_now > t_display + 200 || t_now < t_display)
  {
    update_display(state, temp_set, temp_now, seconds, percent);
    t_display = t_now;
  }
}

String state_name(IRON_STATE state)
{
  switch (state)
  {
    case OFF: return "OFF";
    case CONFIG_PREHEAT_TEMP: return "CONFIG_PREHEAT_TEMP";
    case CONFIG_PREHEAT_TEMP_SET: return "CONFIG_PREHEAT_TEMP_SET";
    case CONFIG_PREHEAT_TIME: return "CONFIG_PREHEAT_TIME";
    case CONFIG_PREHEAT_TIME_SET: return "CONFIG_PREHEAT_TIME_SET";
    case CONFIG_REFLOW_TEMP: return "CONFIG_REFLOW_TEMP";
    case CONFIG_REFLOW_TEMP_SET: return "CONFIG_REFLOW_TEMP_SET";
    case CONFIG_REFLOW_TIME: return "CONFIG_REFLOW_TIME";
    case CONFIG_REFLOW_TIME_SET: return "CONFIG_REFLOW_TIME_SET";
    case CONFIG_START: return "CONFIG_START";
    case PREHEAT: return "PREHEAT";
    case REFLOW: return "REFLOW";
    case COOLING: return "COOLING";
  }
}
void reset_button()
{
  button = 1;
  t_button = t_now + BUTTON_REPRESS_DELAY;
  t_display_flash = 0;
}

IRON_STATE next_config_state(IRON_STATE state, int steps)
{
  return config_state_roll[4 + steps % 5];
}

void flash_encoder_led()
{
  String ledState = t_now >= t_encoder && t_now < t_encoder + ENCODER_LED_FLASH ? "HIGH" : "LOW";
  Serial.println("Encoder LED " + ledState);
  digitalWrite(LED_BUILTIN, t_now >= t_encoder && t_now < t_encoder + ENCODER_LED_FLASH ? HIGH : LOW);
}

int prev_relay = LOW;

void control_iron(int temp, int setpoint)
{
  if (setpoint == 0)
  {
    digitalWrite(RELAY_PIN, LOW);
  }
  else if (setpoint <= temp - offset) {
    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(LED_BUILTIN, LOW);
    if (prev_relay != LOW)
    {
      Serial.println("Heat OFF");
      prev_relay = LOW;
    }
  }
  else if (setpoint > temp + offset) {
    digitalWrite(RELAY_PIN, HIGH);
    digitalWrite(LED_BUILTIN, HIGH);
    if (prev_relay == LOW)
    {
      Serial.println("Heat ON");
      prev_relay = HIGH;
    }
  }
}

void update_display(IRON_STATE state, int set_temp, int temperature, int seconds, int percentage)
{
  display.clearDisplay();
  display.setTextColor(WHITE, BLACK);
  display.setTextSize(1);
  display.setCursor(0, 0);

  switch (state)
  {
    case CONFIG_PREHEAT_TEMP:
    case CONFIG_PREHEAT_TEMP_SET:
    case CONFIG_PREHEAT_TIME:
    case CONFIG_PREHEAT_TIME_SET:
    case CONFIG_REFLOW_TEMP:
    case CONFIG_REFLOW_TEMP_SET:
    case CONFIG_REFLOW_TIME:
    case CONFIG_REFLOW_TIME_SET:
    case CONFIG_START:
      display.print("Preheat: ");
      if (state == CONFIG_PREHEAT_TEMP)
      {
        display.setTextColor(BLACK, WHITE);
      }
      else if (state == CONFIG_PREHEAT_TEMP_SET)
      {
        set_display_flash();
      }
      display.print(String(temp_preheat));
      display.setTextColor(WHITE, BLACK);
      display.print("°C ");
      if (state == CONFIG_PREHEAT_TIME)
      {
        display.setTextColor(BLACK, WHITE);
      }
      else if (state == CONFIG_PREHEAT_TIME_SET)
      {
        set_display_flash();
      }
      display.print(String(time_preheat / 1000));
      display.setTextColor(WHITE, BLACK);
      display.println(" sec");

      display.print("Reflow: ");
      if (state == CONFIG_REFLOW_TEMP)
      {
        display.setTextColor(BLACK, WHITE);
      }
      else if (state == CONFIG_REFLOW_TEMP_SET)
      {
        set_display_flash();
      }
      display.print(String(temp_reflow));
      display.setTextColor(WHITE, BLACK);
      display.print("°C  ");
      if (state == CONFIG_REFLOW_TIME)
      {
        display.setTextColor(BLACK, WHITE);
      }
      else if (state == CONFIG_REFLOW_TIME_SET)
      {
        set_display_flash();
      }
      display.print(String(time_reflow / 1000));
      display.setTextColor(WHITE, BLACK);
      display.println(" sec");

      if (state == CONFIG_START)
      {
        display.setTextColor(BLACK, WHITE);
      }
      display.println("START");
      display.setTextColor(WHITE, BLACK);

      break;

    default:
      display.print(String(state));
    
      String str = String(set_temp) + "°C";
      display.println(str);
    
      if (seconds != 0) {
        str = String(seconds) + " sec";
        display.print(str);
      }
    
      if (percentage != 0) {
        str = String(percentage) + "%";
        display.print(str);
      }
    
      display.setTextSize(2);
      display.setCursor(30, 22);
      str = String(temperature) + "°C";
      display.print(str);
  }

  display.display();
}

void set_display_flash()
{
  if (t_now >= t_display_flash + DISPLAY_FLASH_DELAY)
  {
    display.setTextColor(WHITE, WHITE);
  }
  else
  {
    display.setTextColor(BLACK, WHITE);
  }
  t_display_flash += DISPLAY_FLASH_DELAY;
}
