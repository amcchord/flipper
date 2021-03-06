/*
  Another example adapted from ugfx http://ugfx.org
  This actually shows a problem of RA8875 library, writing
  one pixel it's fast but write many single pixels in the same time
  not and speed suffer.
  It shoul be a workaround that I'm actually study, this will fix 
  also loading big images from SD memory card and so on.

 Works with Arduino 1.0.6 IDE, Arduino 1.6.x IDE
*/
#include <SPI.h>
#include <RA8875.h>

/*
Teensy3.x
You are using 4 wire SPI here, so:
 MOSI:  11//Teensy3.x
 MISO:  12//Teensy3.x
 SCK:   13//Teensy3.x
 the rest of pin below:
 */

#define RA8875_CS 10 //see below...
/*
Teensy 3.x can use: 2,6,9,10,15,20,21,22,23
*/
#define RA8875_RESET 9//any pin or nothing!

RA8875 tft = RA8875(RA8875_CS,RA8875_RESET);//Teensy3/arduino's



const int MAX = 256;
float cx, cy;
float zoom = 1.0f;

void setup() 
{
  Serial.begin(9600);
  //while (!Serial) {;}
  Serial.println("RA8875 start");

  tft.begin(RA8875_800x480);

  cx = -0.086f;
  cy = 0.85f;
}


void loop() 
{
  mandelbrot(-2.0f*zoom+cx, -1.5f*zoom+cy, 2.0f*zoom+cx, 1.5f*zoom+cy);
  zoom *= 0.7f;
  if(zoom <= 0.00001f)
    zoom = 1.0f;	
}

void mandelbrot(float x1, float y1, float x2, float y2) {
  unsigned int i,j;
  uint16_t iter;
  uint16_t color;

  float sy = y2 - y1;
  float sx = x2 - x1;

  for(i = 0; i < tft.width(); i++) {
    for(j = 0; j < tft.height(); j++) {
      float cy = j * sy / tft.height() + y1;
      float cx = i * sx / tft.width() + x1;
      float x = 0.0f, y = 0.0f, xx = 0.0f, yy = 0.0f;
      for(iter=0; iter <= MAX && (xx + yy) < 4.0f; iter++) {
        xx = x * x;
        yy = y * y;
        y = 2.0f * x * y + cy;
        x = xx - yy + cx;
      }
      color = ((iter<<7 & 0xF8) << 8) | ((iter<<4 & 0xFC) << 3) | (iter >> 3);
      tft.drawPixel(i, j, color);
    }
  }
}

