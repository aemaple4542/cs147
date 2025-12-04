#include <Arduino.h>
#include <SparkFunLSM6DSO.h>
#include "Button2.h"
#include "Wire.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <HttpClient.h>
#include <WiFi.h>

#define BLUE_CALIBRATION_PIN 2
int calibration_led_state = LOW;

#define RED_PIN 32
#define YELLOW_PIN 33
#define GREEN_PIN 25

#define RED_STATE 0
#define RED_YELLOW_STATE 1
#define YELLOW_STATE 2
#define GREEN_STATE 3
#define NO_LIGHT_STATE 4

#define LEFT_BUTTON_PIN 4
#define RIGHT_BUTTON_PIN 5
#define BUZZER_PIN 26

// STATES OF THE OVERALL WORKOUT
#define IDLE_STATE 9
#define CALIBRATION_STATE 10
#define WORKOUT_STATE 11
#define REST_STATE 12

// TIME in milliseconds
#define CALLIBRATION_MILLIS 10000
#define BUZZER_MILLIS 1000
#define SAMPLE_INTERVAL_MS 100

#define ANGLE_THRESHOLD 5

// buzzer
bool buzz_state;
unsigned long buzz_timer;

bool button_state;
Button2 left_button;
Button2 right_button;

int session_threshold = 15000;
int default_session_threshold = 15000;

double total = 0.0;
double total_percentage = 0.0;
double score = 0.0;
double max_score = 0.0;

int light_state;
int workout_state; // Workout light state.
unsigned long workout_timer; // Workout light timer.

LSM6DSO gyroscope; 

float pitch = 0;
float target_pitch = 0; // The angle we lock in during calibration
bool target_pitch_determined = false;

unsigned long last_gyro_time;
float dt; 
float alpha = 0.98; // Trust Gyro 98%, Accelerometer 2%

char ssid[50];
char pass[50];
char uriPath[100];

void nvs_access() {
  // Initialize NVS
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was truncated and needs to be erased
    // Retry nvs_flash_init
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
    
  // Open
  Serial.printf("\n");
  Serial.printf("Opening Non-Volatile Storage (NVS) handle... ");
  nvs_handle_t my_handle;
  err = nvs_open("storage", NVS_READWRITE, &my_handle);
  if (err != ESP_OK) {
    Serial.printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
  } else {
    Serial.printf("Done\n");
    Serial.printf("Retrieving SSID/PASSWD\n");
    size_t ssid_len;
    size_t pass_len;
    err = nvs_get_str(my_handle, "ssid", ssid, &ssid_len);
    err |= nvs_get_str(my_handle, "pass", pass, &pass_len);
    switch (err) {
      case ESP_OK:
        Serial.printf("Done\n");
        break;
      case ESP_ERR_NVS_NOT_FOUND:
        Serial.printf("The value is not initialized yet!\n");
        break;
      default:
        Serial.printf("Error (%s) reading!\n", esp_err_to_name(err));
    }
  }
  // Close
  nvs_close(my_handle);
}

void sendDataToAWS() {
  int err = 0;

  WiFiClient c;
  HttpClient http(c);

  int session_percentage = (int)((score / max_score) * 100);
  int session_number = (int) total;

  delay(1000);
  sprintf(uriPath, "/?session_percentage=%d&session_number=%d", session_percentage, session_number);

  Serial.println(uriPath);

  err = http.get("52.53.184.67", 5000, uriPath, NULL);

  if (err == 0) {
    Serial.println("startedRequest ok");
  } else {
    Serial.print("Connect failed: ");
    Serial.println(err);
  }
  http.stop();
}

void startBuzzer() {
  tone(BUZZER_PIN, 417);
  buzz_state = true;
  buzz_timer = millis() + BUZZER_MILLIS;
}

void stopBuzzer() {
  if (buzz_state == true) {
    noTone(BUZZER_PIN);
    buzz_state = false;
  }
}

void handleRightButtonSingleClick(Button2& b) {
  Serial.println("Right Button Pressed");

  if(button_state) {
    switch (workout_state)
    {
      case IDLE_STATE:
        workout_state = CALIBRATION_STATE;
        workout_timer = millis() + CALLIBRATION_MILLIS;
        button_state = false;
        break;

      case CALIBRATION_STATE:
        workout_state = WORKOUT_STATE;
        session_threshold = max(session_threshold, default_session_threshold);
        Serial.println("Session Threshold");
        Serial.println(session_threshold);  

        workout_timer = millis() + session_threshold;
        light_state = GREEN_STATE;

        button_state = false;
        score = 0.0;
        max_score = 0.0;
        total += 1.0;

        startBuzzer();
        break;
      
      case WORKOUT_STATE:
        workout_state = REST_STATE;
        light_state = RED_STATE;

        Serial.println("SESSION SKIPPED");
        Serial.print("SESSION SCORE: ");
        Serial.println(score);
        Serial.print("MAX SCORE: ");
        Serial.println(max_score);
        Serial.print("Score Percentage: ");
        Serial.println(score / max_score);

        total_percentage += (score / max_score);

        startBuzzer();
        break;
      
      case REST_STATE:
        workout_state = WORKOUT_STATE;
        workout_timer = millis() + session_threshold;
        light_state = GREEN_STATE;
  
        button_state = false;
        score = 0.0;
        max_score = 0.0;
        total += 1.0;

        startBuzzer();
        break;
    }
  }
}

void handleRightButtonHold(Button2& b) {
  Serial.println("Right Button Hold");

  if (workout_state == REST_STATE) {
    workout_state = IDLE_STATE;
    light_state = NO_LIGHT_STATE;
    button_state = false;

    stopBuzzer();

    Serial.print("Total Number of Sessions: ");
    Serial.println(total);

    Serial.print("Average Percentage: ");
    Serial.println(total_percentage / total);
  }
}

void handleLeftButtonSingleClick(Button2& b) {
  Serial.println("Left Button Single Click");

  if (workout_state == CALIBRATION_STATE)
  {
    session_threshold += 5000;
    Serial.println(session_threshold);
  } else if (workout_state == REST_STATE) {
    Serial.println("SENDING DATA TO AWS");
    
    // TODO: SEND DATA TO AWS OF ONE EXERCISE
    sendDataToAWS();
  }
}

void handleLeftButtonDoubleClick(Button2& b) {
  Serial.println("Left Button Double Click");

  if (workout_state == CALIBRATION_STATE)
  {
    session_threshold -= 5000;
    Serial.println(session_threshold);
  }
}


void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  Serial.println("STARTED SETUP");

  nvs_access();
  // Inside setup()
  WiFi.begin(ssid, pass);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Configure LED pins as outputs.
  pinMode(BLUE_CALIBRATION_PIN, OUTPUT);
  pinMode(RED_PIN, OUTPUT);
  pinMode(YELLOW_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);

  // Turn all lights off
  digitalWrite(BLUE_CALIBRATION_PIN, LOW);
  digitalWrite(RED_PIN, LOW);
  digitalWrite(YELLOW_PIN, LOW);
  digitalWrite(GREEN_PIN, LOW);

  // Configure Button Pin
  right_button.setClickHandler(handleRightButtonSingleClick);
  right_button.setLongClickHandler(handleRightButtonHold);
  right_button.begin(RIGHT_BUTTON_PIN);

  left_button.setClickHandler(handleLeftButtonSingleClick);
  left_button.setDoubleClickHandler(handleLeftButtonDoubleClick);
  left_button.begin(LEFT_BUTTON_PIN);

  button_state = true; // DETERMINES WHEN BUTTONS ARE REACTIVE

  // Initial state for states and timers..
  light_state = NO_LIGHT_STATE;
  workout_state = IDLE_STATE;
  workout_timer = millis();
  buzz_timer = millis();

  tone(BUZZER_PIN, 0);

  Wire.begin();
  gyroscope.begin();
  gyroscope.initialize(BASIC_SETTINGS);

  Serial.println("FINISHED SETUP");
}

void loop() {
  left_button.loop();
  right_button.loop();

  unsigned long current_time = millis();

  if (current_time - last_gyro_time >= SAMPLE_INTERVAL_MS) {
    dt = (current_time - last_gyro_time) / 1000.0; 
    last_gyro_time = current_time;

    float gyroY = gyroscope.readFloatGyroY();
    float accelX = gyroscope.readFloatAccelX();
    float accelZ = gyroscope.readFloatAccelZ();

    // Calculate Pitch (Tilt forward/back)
    // atan2(accelY, accelZ) usually gives the pitch if the chip is flat on the leg
    float accelPitch = atan2(accelX, accelZ) * 180.0 / PI;

    // Complementary Filter
    pitch = alpha * (pitch + gyroY * dt) + (1.0 - alpha) * accelPitch;
  }

  // put your main code here, to run repeatedly:
  switch (workout_state)
  {
    case IDLE_STATE:
      break;

    case CALIBRATION_STATE:
      calibration_led_state = ((millis() / 2000) % 2 == 0) ? HIGH : LOW;    

      if (millis() > workout_timer) 
      {
        if (!target_pitch_determined) {
          target_pitch = pitch;
          Serial.print("CALIBRATION DONE. TARGET LOCKED: ");
          Serial.println(target_pitch);

          target_pitch_determined = true;
        }

        calibration_led_state = LOW;
        button_state = true;
      }

      digitalWrite(BLUE_CALIBRATION_PIN, calibration_led_state);
      break;
    
    case WORKOUT_STATE:
      if (millis() > workout_timer) {
        light_state = RED_STATE;
        button_state = true;
        workout_state = REST_STATE;

        startBuzzer();

        Serial.print("SESSION FINISHED");
        Serial.print("SESSION SCORE: ");
        Serial.println(score);
        Serial.print("MAX SCORE: ");
        Serial.println(max_score);
        Serial.print("Score Percentage: ");
        Serial.println(score / max_score);
        
        total_percentage += (score / max_score);

      } else if (workout_timer - 5000 < millis()) {
        light_state = RED_YELLOW_STATE;
      } else if(workout_timer - 10000 < millis()) {
        light_state = YELLOW_STATE;
      } 

      if(abs(pitch - target_pitch) <= ANGLE_THRESHOLD) {
        score += 1.0;
      }

      max_score += 1.0;

      break;
    
    case REST_STATE:
      break;
  }

  switch (light_state) {
    case RED_STATE:
      digitalWrite(RED_PIN, HIGH);
      digitalWrite(YELLOW_PIN, LOW);
      digitalWrite(GREEN_PIN, LOW);
      break;
    
    case RED_YELLOW_STATE:
      digitalWrite(RED_PIN, HIGH);
      digitalWrite(YELLOW_PIN, HIGH);
      digitalWrite(GREEN_PIN, LOW);
      break;

    case YELLOW_STATE:
      digitalWrite(RED_PIN, LOW);
      digitalWrite(YELLOW_PIN, HIGH);
      digitalWrite(GREEN_PIN, LOW);
      break;

    case GREEN_STATE:
      digitalWrite(RED_PIN, LOW);
      digitalWrite(YELLOW_PIN, LOW);
      digitalWrite(GREEN_PIN, HIGH);
      break;
    
    case NO_LIGHT_STATE:
      digitalWrite(RED_PIN, LOW);
      digitalWrite(YELLOW_PIN, LOW);
      digitalWrite(GREEN_PIN, LOW);
      break;
  }

  if (buzz_state && millis() > buzz_timer) {
    stopBuzzer();
  }
}