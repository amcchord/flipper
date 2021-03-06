
#include <SPI.h>
#include <i2c_t3.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <PulsePosition.h>
#include <Servo.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>
#include <PID_v1.h>
#include <PWMServo.h>
#include <Adafruit_DotStarMatrix.h>
#include <Adafruit_DotStar.h>
#include <Adafruit_MotorShield.h>
#include "utility/Adafruit_MS_PWMServoDriver.h"

//Reboot Mode
#define CPU_RESTART_ADDR (uint32_t *)0xE000ED0C
#define CPU_RESTART_VAL 0x5FA0004
#define CPU_RESTART (*CPU_RESTART_ADDR = CPU_RESTART_VAL);

Adafruit_SSD1306 display(29);
String modeString = "";
String armString = "";
#if (SSD1306_LCDHEIGHT != 32)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

//AHRS Vars
#define BNO055_SAMPLERATE_DELAY_MS (1)
Adafruit_BNO055 bno = Adafruit_BNO055(55,0x28);
bool inverted = 0;
double targetHeading = 0;
double targetBalance = 5;
double headingDelta = 0;

//Heading PID
double consKp=0.7, consKi=.2, consKd=0.01;
double Setpoint, Input, Output;
PID headingPID(&Input, &Output, &Setpoint, consKp, consKi, consKd, DIRECT);

#define TURN_SENSITIVITY 60.0 //The lower this number the more sensitive we are to
                              //Turn Inputs
#define THROTTLESTART 40      //This is the PWM level that actually makes the wheels
                              //move
#define H_HOLD_TIME 500       //Milliseconds to hold in heading mode after 0 throttle

unsigned long hstart = 0;


//Balance PIDs
double balKp=0.7, balKi=.2, balKd=0.01;
double balSet, balIn, balOut;
PID balancePID(&balIn, &balOut, &balSet, balKp, balKi, balKd, DIRECT);

//Travel PIDs
double trvKp=0.7, trvKi=.2, trvKd=0.01;
double trvSet, trvIn, trvOut;
PID travelPID(&trvIn, &trvOut, &trvSet, trvKp, trvKi, trvKd, DIRECT);



//Trim for RC Inputs
const int rcMin = 1099;
const int rcMax = 1920;
int rcScale = rcMax - rcMin;
#define FAILSAFE false //Failsafe is disabled for now
#define DEADBAND 20 //If thrust values are within +/-10 of 0 assume they are 0
#define REJECTTHRESH 2200 //Rc values above this number are considered invalid
#define RCVR_PPM 23 //Pin where the PPM comes in

//Create some global variables to store the state of RC Reciver Channels
double rc1 = 0; // Turn
double rc2 = 0; // Thrust
double rc3 = 0; //
double rc4 = 0; //
double rc5 = 0; // Mode
double rc6 = 0; // Safety

//Define the PPM decoder object
PulsePositionInput myIn;
PWMServo leftFlipper;
PWMServo rightFlipper;
int leftLevel = 0;
int rightLevel = 0;
//Define the ports that control the motors
//Motor Driver Outputs



Adafruit_MotorShield AFMS = Adafruit_MotorShield();
Adafruit_DCMotor *leftMotor = AFMS.getMotor(1);
Adafruit_DCMotor *leftMotorRear = AFMS.getMotor(2);
Adafruit_DCMotor *rightMotor = AFMS.getMotor(3);
Adafruit_DCMotor *rightMotorRear = AFMS.getMotor(4);

Adafruit_DotStarMatrix leftEye = Adafruit_DotStarMatrix(
  8, 8, 9, 10,
  DS_MATRIX_TOP     + DS_MATRIX_RIGHT +
  DS_MATRIX_COLUMNS + DS_MATRIX_PROGRESSIVE,
  DOTSTAR_BRG);

  Adafruit_DotStarMatrix rightEye = Adafruit_DotStarMatrix(
    8, 8, 15, 14,
    DS_MATRIX_TOP     + DS_MATRIX_RIGHT +
    DS_MATRIX_COLUMNS + DS_MATRIX_PROGRESSIVE,
    DOTSTAR_BRG);



//Some globals for handling mode switching
int lastMode = 0;
int pixelTicker = 0;

int left = 0;
int right = 0;

int count = 10;
imu::Vector<3> euler;


IntervalTimer gyroSafety;

void setup()   {
  pinMode(9, INPUT_PULLDOWN);
  pinMode(10, INPUT_PULLDOWN);
  pinMode(14, INPUT_PULLDOWN);
  pinMode(15, INPUT_PULLDOWN);

  leftFlipper.attach(3);
  rightFlipper.attach(4);
  leftFlipper.write(97);
  rightFlipper.write(108);

  leftEye.begin();
  leftEye.setRotation(3);
  leftEye.setTextWrap(false);
  leftEye.setBrightness(10);
  leftEye.fillScreen(0);
  leftEye.show();

  rightEye.begin();
  rightEye.setRotation(3);
  rightEye.setTextWrap(false);
  rightEye.setBrightness(10);
  rightEye.fillScreen(0);
  rightEye.show();

  pinMode(RCVR_PPM, INPUT_PULLDOWN);

  pinMode(13,OUTPUT); //Just make sure we can use the onboard LED for stuff

  Serial.begin(9600);
  myIn.begin(RCVR_PPM);
  // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  Wire.setClock(3200000);
  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.println("Booting...");
  display.display();

  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.println("Gyro Calibartion...");
  display.display();


  delay(100);
  gyroSafety.begin(gSafety, 2000000); // Call the safety function after 2 seocnds
  gyroSafety.priority(10);
  if(!bno.begin())
  {
    display.clearDisplay();
    display.setCursor(0,0);
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.println("Gyro FAIL!");
    display.display();
    delay(1000);
  }
  gyroSafety.end();

  digitalWrite(13, HIGH);
  delay(1000);
  digitalWrite(13, LOW);



  //Okay lets start the PID
  imu::Vector<3> euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
  euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
  targetHeading = euler.x();

  //Arm the motors!
  AFMS.begin();

  levelFlippers();
  headingPID.SetMode(AUTOMATIC);
  headingPID.SetSampleTime(10);
  headingPID.SetOutputLimits(-200, 200);

}

int thrust;
int turn;
void loop() {
  updateChannels();
  eyeControl(0);


  display.clearDisplay();

  //Scale the raw RC input
   thrust = round(((rc2 - rcMin)/rcScale) * 500) - 250; //Cast to -250-0-250
   turn = round(((rc1 - rcMin)/rcScale) * 500) - 250; //Cast to -250-0-250
  int flipper = round(((rc3 - rcMin)/rcScale) * 180) - 90; //Cast to 0-256
  int yaw = round(((rc4 - rcMin)/rcScale) * 180) - 90; //Cast to 0-256
  int mode = round(((rc5 - rcMin)/rcScale) * 4); //Cast to 0-256
  int safety = round(((rc6 - rcMin)/rcScale) * 256); //Cast to 0-256



  if (mode == 0){
    modeString = "Direct";
  }
  else if (mode == 2){
    modeString = "Heading";
  }

  else if (mode == 4){
    modeString = "Full";
  }

  if (safety > 200){
    armString = "ARM";
  } else {
    armString = "Safe";
    //Stop all the motors.
    leftMotorRear->run(RELEASE);
    leftMotor->run(RELEASE);
    rightMotor->run(RELEASE);
    rightMotorRear->run(RELEASE);
  }


  //Apply Deadband Correction
  if (thrust < DEADBAND && thrust > (DEADBAND * -1)){
    thrust = 0;
  }
  if (turn < DEADBAND && turn > (DEADBAND * -1)){
    turn = 0;
  }

  euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);


  if (count == 10 ){ //Only update the display once every 10 loops
    updateDisplay();
    count = 0;
  }
  count++;




  if (mode == 0){
    simpleDrive(thrust, turn);

  }

  else if (mode == 2){
    //driveAsist(thrust, turn);
    simpleDrive(thrust, turn);
  }

  else if (mode == 4){

    //fullAuto(thrust, turn);
    simpleDrive(thrust, turn);

  }

//  setFlippers(flipper, yaw);



}

void levelFlippers(){
    euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
    int x = 90;
    bool escapeThis = false;
    double eulerBase = euler.z();
    while (x < 140 && escapeThis == false ){
      euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);

      leftFlipper.write(x);
      delay(100);
      display.clearDisplay();
      display.setCursor(0,0);
      display.setTextSize(1);
      display.println(x);
      display.println(eulerBase);
      display.println(euler.z());
      display.display();
      if (eulerBase - euler.z() > 1 || eulerBase - euler.z() < -1 ){
        display.clearDisplay();
        display.setCursor(0,0);
        display.setTextSize(1);
        display.println("HIT!");
        display.println(eulerBase);
        display.println(euler.z());
        display.println(eulerBase - euler.z());
        display.display();
        escapeThis = true;
        leftFlipper.write(80);
        delay(500);
        leftFlipper.write(x-4);
        leftLevel = x - 4;
      }
      x++;
    }
    x = 110;
    euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
    eulerBase = euler.z();
    escapeThis = false;
    while (x > 60 && escapeThis == false ){
      euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);

      rightFlipper.write(x);
      delay(100);
      display.clearDisplay();
      display.setCursor(0,0);
      display.setTextSize(1);
      display.println(x);
      display.println(eulerBase);
      display.println(euler.z());
      display.display();
      if (eulerBase - euler.z() > 1 || eulerBase - euler.z() < -1 ){
        display.clearDisplay();
        display.setCursor(0,0);
        display.setTextSize(1);
        display.println("HIT!");
        display.println(eulerBase);
        display.println(euler.z());
        display.println(eulerBase - euler.z());
        display.display();
        escapeThis = true;
        rightFlipper.write(120);
        delay(500);
        rightFlipper.write(x+4);
        rightLevel = x +4;
      }
      x--;
    }
}

void setFlippers(int flip, int shift){

  int leftFlip = 90;
  int rightFlip = 90;

  leftFlip = leftFlip + shift + flip;
  rightFlip = rightFlip + shift - flip;

  leftFlipper.write(leftFlip);
  rightFlipper.write(rightFlip);
}

void updateDisplay(){
  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextSize(1);
  display.print(armString);
  display.setCursor(64,0);
  display.setTextSize(1);
  display.print(modeString);
  display.setCursor(0,8);
  display.setTextSize(1);
  display.print("");
  display.print((int)thrust);
  display.print(" / ");
  display.println((int)turn);


  display.print("Lft:  ");
  display.println(left);
  display.print("Rgt: ");
  display.println(right);

  display.setCursor(64,8);
  display.print("X: ");
  display.println(euler.x());
  display.setCursor(64,16);
  display.print("Z: ");
  display.println(euler.z());
  display.setCursor(64,24);
  display.print("T: ");
  display.print(leftLevel);
  display.print(" ");
  display.print(rightLevel);
  display.display();

}

void fullAuto(double thrust, double turn){

  //Lets run away from areas where we might fall over!
  double tilt = euler.y();
  double thrustAdjust;
  if (tilt < -20){
    thrustAdjust = -100 + tilt * 2;
  }
  else if (tilt > 20){
    thrustAdjust = 100 + tilt * 2;
  }
  thrust = thrust + thrustAdjust;
  if (thrust > 250){
    thrust = 250;
  }
  else if (thrust < -250){
    thrust = -250;
  }
  driveAsist(thrust, turn);

}

void driveAsist(double thrust, double turn){
  double currentHeading = euler.x();
  int throttleAssist = THROTTLESTART;

  if (thrust < 20 && hstart + H_HOLD_TIME < millis()){
    targetHeading = currentHeading;
    if (turn < 0 ){
      turn = ((turn * turn) / 250) * -1;
    }
    else if (turn > 0 ){
      turn = ((turn * turn) / 250);
    }
    simpleDrive(thrust, turn);
  }
  else {
    hstart = millis();

    //Turn input now shifts our target heading
    targetHeading = targetHeading + (turn/TURN_SENSITIVITY) * -1;
    if (targetHeading < 0){
      targetHeading = targetHeading + 360;
    }
    if (targetHeading > 360){
      targetHeading = targetHeading - 360;
    }

    //Figure out how far off couse we are.. heading different needs to correct
    //for the fact we can never be more than 180 degrees off target.
    if (targetHeading - currentHeading < -180){
      headingDelta = targetHeading - currentHeading + 360;
    }
    else if (targetHeading - currentHeading > 180 ){
      headingDelta = targetHeading - currentHeading - 360;
    }
    else {
      headingDelta = targetHeading - currentHeading;
    }

    Setpoint = 0;
    Input = headingDelta;
    headingPID.Compute();
    turn = Output;
  }

  double finalOutput = 0;
  //If our outputs are below the throttleAssist level lets give them a boost
  if (thrust < 0){
    throttleAssist = throttleAssist + thrust;
  } else if (thrust > 0) {
    throttleAssist = throttleAssist - thrust;
  }
  if (throttleAssist < 0){
    throttleAssist = 0;
  }
  if (turn > 0){
    finalOutput = turn + throttleAssist;
  } else if (turn < 0) {
    finalOutput = turn - throttleAssist;
  }

  simpleDrive(thrust, finalOutput);

}

void simpleDrive(double thrust, double turn){
  left = 0;
  right = 0;
  turn = turn;

  //This is where the turning logic is.. That's it.
  left = thrust + turn;
  right = thrust - turn;


  //Safety checks!
  if (left > 255){
    left = 255;
  }
  else if (left < -255){
    left = -255;
  }

  //If the left motor needs to go forward.
    if (left > 0){
      leftMotor->setSpeed(left);
      leftMotor->run(FORWARD);

      leftMotorRear->setSpeed(left);
      leftMotorRear->run(BACKWARD);

    } else { //Left motor needs to spin backward
      leftMotor->setSpeed(left * -1);
      leftMotor->run(BACKWARD);

      leftMotorRear->setSpeed(left * -1);
      leftMotorRear->run(FORWARD);
    }


    //Same thing for the right side
    if (right > 255){
      right = 255;
    }
    else if (right < -255){
      right = -255;
    }

    if (right > 0){
      rightMotor->setSpeed(right);
      rightMotor->run(FORWARD);

      rightMotorRear->setSpeed(right);
      rightMotorRear->run(BACKWARD);

    } else {
      rightMotor->setSpeed(right * -1);
      rightMotor->run(BACKWARD);

      rightMotorRear->setSpeed(right * -1);
      rightMotorRear->run(FORWARD);
    }
}

//Read in the channels from the RC reciver
void updateChannels(){

  int num = myIn.available();
  if (num > 0) {

    int rc1t = myIn.read(1);
    int rc2t = myIn.read(2);
    int rc3t = myIn.read(3);
    int rc4t = myIn.read(4);
    int rc5t = myIn.read(5);
    int rc6t = myIn.read(6);

    //Don't register weird outliers!
    if (rc1t > 0 && rc1t < REJECTTHRESH){
      rc1 = rc1t;
    }
    if (rc2t > 0 && rc2t < REJECTTHRESH){
      rc2 = rc2t;
    }
    if (rc3t > 0 && rc3t < REJECTTHRESH){
      rc3 = rc3t;
    }
    if (rc4t > 0 && rc4t < REJECTTHRESH){
      rc4 = rc4t;
    }
    if (rc5t > 0 && rc5t < REJECTTHRESH){
      rc5 = rc5t;
    }
    if (rc6t > 0 && rc6t < REJECTTHRESH){
      rc6 = rc6t;
    }

    if (rc6 > 2000 && FAILSAFE){ //Will shutdown is reciever is programed correctly for failsafe
      rc1 = 0;
      rc2 = 0;
      rc3 = 0;
      rc4 = 0;
      rc5 = 0;
      rc6 = 0;
    }
  }
}

void eyeControl (int mode){
  if (mode == 0){ //Normal Eyes
    leftEye.setTextWrap(false);
    leftEye.setBrightness(20);
    leftEye.setTextColor(leftEye.Color(255, 0, 0));

    leftEye.fillScreen(0);
    leftEye.setCursor(0, 0);
    leftEye.fillRoundRect(0, 0, 8, 8, 4, leftEye.Color(64, 64, 128));
    leftEye.fillRoundRect(3, 2, 2, 2, 0, leftEye.Color(0, 0, 0));
    leftEye.show();

    rightEye.setTextWrap(false);
    rightEye.setBrightness(20);
    rightEye.setTextColor(leftEye.Color(255, 0, 0));

    rightEye.fillScreen(0);
    rightEye.fillRoundRect(0, 0, 8, 8, 4, leftEye.Color(64, 64, 128));
    rightEye.fillRoundRect(3, 2, 2, 2, 0, leftEye.Color(0, 0, 0));
    rightEye.show();
  }
  else if (mode == 1){ //Angry eyes
    leftEye.setTextWrap(false);
    leftEye.setBrightness(20);
    leftEye.setTextColor(leftEye.Color(255, 0, 0));

    leftEye.fillScreen(0);
    leftEye.setCursor(0, 0);
    leftEye.fillRoundRect(0, 0, 8, 8, 4, leftEye.Color(64, 64, 128));
    leftEye.fillRoundRect(3, 2, 2, 2, 0, leftEye.Color(0, 0, 0));
    leftEye.fillTriangle(0,7,7,7,7,4,leftEye.Color(0, 0, 0));
    leftEye.show();

    rightEye.setTextWrap(false);
    rightEye.setBrightness(20);
    rightEye.setTextColor(leftEye.Color(255, 0, 0));

    rightEye.fillScreen(0);
    rightEye.fillRoundRect(0, 0, 8, 8, 4, leftEye.Color(64, 64, 128));
    rightEye.fillRoundRect(3, 2, 2, 2, 0, leftEye.Color(0, 0, 0));
    rightEye.fillTriangle(0,7,7,7,0,4,leftEye.Color(0, 0, 0));
    rightEye.show();
  }
  else  if (mode == 2){ //Angry red eyes
    leftEye.setTextWrap(false);
    leftEye.setBrightness(20);
    leftEye.setTextColor(leftEye.Color(255, 0, 0));

    leftEye.fillScreen(0);
    leftEye.setCursor(0, 0);
    leftEye.fillRoundRect(0, 0, 8, 8, 4, leftEye.Color(32, 128, 0));
    leftEye.fillRoundRect(3, 2, 2, 2, 0, leftEye.Color(0, 0, 0));
    leftEye.fillTriangle(0,7,7,7,7,4,leftEye.Color(0, 0, 0));
    leftEye.show();

    rightEye.setTextWrap(false);
    rightEye.setBrightness(20);
    rightEye.setTextColor(leftEye.Color(255, 0, 0));

    rightEye.fillScreen(0);
    rightEye.fillRoundRect(0, 0, 8, 8, 4, leftEye.Color(32, 128, 0));
    rightEye.fillRoundRect(3, 2, 2, 2, 0, leftEye.Color(0, 0, 0));
    rightEye.fillTriangle(0,7,7,7,0,4,leftEye.Color(0, 0, 0));
    rightEye.show();
  }

}


void gSafety(){
  //We are here because the Gyro hung... we are going to reboot!
   CPU_RESTART
   delay(1000);
}
