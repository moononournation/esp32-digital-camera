#include <esp_camera.h>
#include <SD.h>
#include <rom/tjpgd.h>
#include <Arduino_HWSPI.h>
#include <Arduino_Display.h> // Various display driver

#define CAMERA_MODEL_TTGO_T_CAMERA // check camera_pins.h for other camera model
#include "camera_pins.h"
#include "tjpgdec.h"

#define PREVIEW_QUALITY 63 // 1-63, 1 is the best
#define SNAP_QUALITY 6     // 1-63, 1 is the best
#define SNAP_SIZE FRAMESIZE_UXGA

#define TFT_BL 2
#define SCK 21
#define MOSI 19
#define MISO 22
#define SS 0
Arduino_HWSPI *bus = new Arduino_HWSPI(15 /* DC */, 12 /* CS */, SCK, MOSI, MISO);
Arduino_ST7789 *gfx = new Arduino_ST7789(bus, -1 /* RST */, 2 /* rotation */, true /* IPS */, 240 /* width */, 240 /* height */, 0 /* col offset 1 */, 80 /* row offset 1 */);

char tmpStr[256];
char nextFilename[31];
uint16_t fileIdx = 0;
int i = 0;
uint16_t *preview;
sensor_t *s;
camera_fb_t *fb = NULL;
JPGIODEV dev;
char *work = NULL; // Pointer to the working buffer (must be 4-byte aligned)

void setup()
{
  Serial.begin(115200);
  // Serial.setDebugOutput(true);

  gfx->begin();
  gfx->fillScreen(BLACK);

#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif

  gfx->setTextSize(2);
  gfx->setTextColor(WHITE, RED);
  gfx->setCursor(0, 0);
  gfx->print(" ESP");
  gfx->setTextColor(WHITE, ORANGE);
  gfx->setCursor(48, 0);
  gfx->print("32 ");
  gfx->setTextColor(WHITE, DARKGREEN);
  gfx->setCursor(80, 0);
  gfx->print(" Camera ");
  gfx->setTextColor(WHITE, BLUE);
  gfx->setCursor(168, 0);
  gfx->print(" Plus ");
  gfx->setTextColor(WHITE, BLACK);
  gfx->setTextSize(1);

  if (!SD.begin(SS, SPI, 80000000))
  {
    gfx->setCursor(0, 208);
    gfx->print("SD Init Fail!");
    Serial.println("SD Init Fail!");
  }
  else
  {
    snprintf(tmpStr, sizeof(tmpStr), "SD Card Type: %d Size: %lu MB", SD.cardType(), SD.cardSize() / 1024 / 1024);
    gfx->setCursor(0, 208);
    gfx->print(tmpStr);
    Serial.println(tmpStr);

    init_folder();

    xTaskCreate(
        findNextFileIdxTask,   /* Task function. */
        "FindNextFileIdxTask", /* String with name of task. */
        10000,                 /* Stack size in bytes. */
        work,                  /* Parameter passed as input of the task */
        1,                     /* Priority of the task. */
        NULL);                 /* Task handle. */
  }

  esp_err_t err = cam_init();
  if (err != ESP_OK)
  {
    snprintf(tmpStr, sizeof(tmpStr), "Camera init failed with error 0x%x", err);
    gfx->setCursor(0, 208);
    gfx->print(tmpStr);
    Serial.println(tmpStr);
  }

  // drop down frame size for higher initial frame rate
  s = esp_camera_sensor_get();
  s->set_brightness(s, 2);
  s->set_contrast(s, 2);
  s->set_saturation(s, 2);
  s->set_sharpness(s, 2);
  s->set_aec2(s, true);
  s->set_denoise(s, true);
  s->set_lenc(s, true);
  s->set_hmirror(s, true);
  // s->set_vflip(s, true);
  s->set_quality(s, PREVIEW_QUALITY);

  preview = new uint16_t[200 * 150];
  work = (char *)malloc(WORK_BUF_SIZE);
  dev.linbuf_idx = 0;
  dev.x = 0;
  dev.y = 0;
  dev.linbuf[0] = (color_t *)heap_caps_malloc(JPG_IMAGE_LINE_BUF_SIZE * 3, MALLOC_CAP_DMA);
  dev.linbuf[1] = (color_t *)heap_caps_malloc(JPG_IMAGE_LINE_BUF_SIZE * 3, MALLOC_CAP_DMA);
}

esp_err_t cam_init()
{
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  // init with high specs to pre-allocate larger buffers
  config.frame_size = FRAMESIZE_UXGA;
  config.jpeg_quality = SNAP_QUALITY;
  config.fb_count = 2;

  // camera init
  return esp_camera_init(&config);
}

void init_folder()
{
  File file = SD.open("/DCIM");
  if (!file)
  {
    Serial.println("Create /DCIM");
    SD.mkdir("/DCIM");
  }
  else
  {
    Serial.println("Found /DCIM");
    file.close();
  }
  file = SD.open("/DCIM/100ESPDC");
  if (!file)
  {
    Serial.println("Create /DCIM/100ESPDC");
    SD.mkdir("/DCIM/100ESPDC");
  }
  else
  {
    Serial.println("Found /DCIM/100ESPDC");
    file.close();
  }
}

void findNextFileIdxTask(void *parameter)
{
  findNextFileIdx();
  vTaskDelete(NULL);
}

void findNextFileIdx()
{ // TODO: revise ugly code
  fileIdx++;
  File file;
  snprintf(nextFilename, sizeof(nextFilename), "/DCIM/100ESPDC/DSC%05D.JPG", fileIdx);
  file = SD.open(nextFilename);
  if (!file)
  {
    Serial.printf("Next file: %s\n", nextFilename);
    return;
  }
  else
  {
    for (int k = 1000; k <= 30000; k += 1000)
    {
      snprintf(nextFilename, sizeof(nextFilename), "/DCIM/100ESPDC/DSC%05D.JPG", fileIdx + k);
      file = SD.open(nextFilename);
      if (file)
      {
        Serial.printf("Found %s\n", nextFilename);
        file.close();
      }
      else
      {
        Serial.printf("Not found %s\n", nextFilename);
        k -= 1000;
        for (int h = 100; h <= 1000; h += 100)
        {
          snprintf(nextFilename, sizeof(nextFilename), "/DCIM/100ESPDC/DSC%05D.JPG", fileIdx + k + h);
          file = SD.open(nextFilename);
          if (file)
          {
            Serial.printf("Found %s\n", nextFilename);
            file.close();
          }
          else
          {
            Serial.printf("Not found %s\n", nextFilename);
            h -= 100;
            for (int t = 10; t <= 100; t += 10)
            {
              snprintf(nextFilename, sizeof(nextFilename), "/DCIM/100ESPDC/DSC%05D.JPG", fileIdx + k + h + t);
              file = SD.open(nextFilename);
              if (file)
              {
                Serial.printf("Found %s\n", nextFilename);
                file.close();
              }
              else
              {
                Serial.printf("Not found %s\n", nextFilename);
                t -= 10;
                for (int d = 1; d <= 10; d++)
                {
                  snprintf(nextFilename, sizeof(nextFilename), "/DCIM/100ESPDC/DSC%05D.JPG", fileIdx + k + h + t + d);
                  file = SD.open(nextFilename);
                  if (file)
                  {
                    Serial.printf("Found %s\n", nextFilename);
                    file.close();
                  }
                  else
                  {
                    Serial.printf("Next file: %s\n", nextFilename);
                    fileIdx += k + h + t + d;
                    return;
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

void snap()
{
  s->set_hmirror(s, false);
  // s->set_vflip(s, false);
  s->set_quality(s, SNAP_QUALITY);

  gfx->fillRect(20, 45, 200, 150, DARKGREY);

  fb = esp_camera_fb_get();
  esp_camera_fb_return(fb);
  fb = NULL;

  gfx->fillRect(20, 45, 200, 150, LIGHTGREY);

  fb = esp_camera_fb_get();
  if (!fb)
  {
    gfx->setCursor(0, 208);
    gfx->print("Camera capture JPG failed");
    Serial.println("Camera capture JPG failed");
  }
  else
  {
    File file = SD.open(nextFilename, FILE_WRITE);
    if (file.write(fb->buf, fb->len))
    {
      gfx->fillRect(20, 45, 200, 150, LIGHTGREY);
      snprintf(tmpStr, sizeof(tmpStr), "File written: %luKB\n%s", fb->len / 1024, nextFilename);
      gfx->setCursor(0, 224);
      gfx->print(tmpStr);
      Serial.println(tmpStr);
    }
    else
    {
      gfx->setCursor(0, 224);
      gfx->print("Write failed!");
      Serial.println("Write failed!");
    }
    esp_camera_fb_return(fb);
    fb = NULL;
    file.close();
  }

  s->set_hmirror(s, true);
  // s->set_vflip(s, true);
  s->set_quality(s, PREVIEW_QUALITY);
  fb = esp_camera_fb_get();
  esp_camera_fb_return(fb);
  fb = NULL;
}

void enterSleep()
{
  // gfx->end();
  esp_deep_sleep_start();
}

void loop()
{
  if (i == 1) // count down
  {
    gfx->setTextSize(2);
    gfx->setCursor(116, 24);
    gfx->print("3");
    Serial.println("3");
  }
  else if (i == 5)
  {
    gfx->setTextSize(2);
    gfx->setCursor(116, 24);
    gfx->print("2");
    Serial.println("2");
  }
  else if (i == 9)
  {
    gfx->setTextSize(2);
    gfx->setCursor(116, 24);
    gfx->print("1");
    Serial.println("1");
  }
  else if (i == 13) // start snap
  {
    gfx->setTextSize(2);
    gfx->setCursor(92, 24);
    gfx->print("Cheeze!");
    Serial.println("Cheeze!");

    snap();
  }
  else if (i == 15)
  {
    gfx->setTextSize(2);
    gfx->setCursor(92, 24);
    gfx->print("Cheeze!!");
    Serial.println("Cheeze!!");

    findNextFileIdx();
    snap();
  }
  else if (i == 17)
  {
    gfx->setTextSize(2);
    gfx->setCursor(92, 24);
    gfx->print("Cheeze!!!");
    Serial.println("Cheeze!!!");

    findNextFileIdx();
    snap();

    gfx->setTextSize(2);
    gfx->setCursor(0, 24);
    gfx->print("Reset to snap again!");
    Serial.println("Reset to snap again!");

    decodeJpegFile(nextFilename, 3);
    gfx->draw16bitRGBBitmap(20, 45, preview, 200, 150);
    delay(5000);
    Serial.println("Enter deep sleep...");
    enterSleep();
  }
  else
  {
    fb = esp_camera_fb_get();
    if (!fb)
    {
      gfx->setCursor(0, 224);
      gfx->print("Camera capture failed!");
      Serial.printf("Camera capture failed!");
    }
    else
    {
      decodeJpegBuff(fb->buf, fb->len, 3);
      gfx->draw16bitRGBBitmap(20, 45, preview, 200, 150);
      esp_camera_fb_return(fb);
      fb = NULL;
    }
  }

  i++;
}

// User defined call-back function to input JPEG data from file
//---------------------
static UINT tjd_file_input(
    JDEC *jd,   // Decompression object
    BYTE *buff, // Pointer to the read buffer (NULL:skip)
    UINT nd     // Number of bytes to read/skip from input stream
)
{
  int rb = 0;
  // Device identifier for the session (5th argument of jd_prepare function)
  JPGIODEV *dev = (JPGIODEV *)jd->device;

  if (buff)
  { // Read nd bytes from the input strem
    rb = dev->f.read(buff, nd);
    return rb; // Returns actual number of bytes read
  }
  else
  { // Remove nd bytes from the input stream
    if (dev->f.seek(dev->f.position() + nd))
      return nd;
    else
      return 0;
  }
}

// User defined call-back function to input JPEG data from memory buffer
//-------------------------
static UINT tjd_buf_input(
    JDEC *jd,   // Decompression object
    BYTE *buff, // Pointer to the read buffer (NULL:skip)
    UINT nd     // Number of bytes to read/skip from input stream
)
{
  // Device identifier for the session (5th argument of jd_prepare function)
  JPGIODEV *dev = (JPGIODEV *)jd->device;
  if (!dev->membuff)
    return 0;
  if (dev->bufptr >= (dev->bufsize + 2))
    return 0; // end of stream

  if ((dev->bufptr + nd) > (dev->bufsize + 2))
    nd = (dev->bufsize + 2) - dev->bufptr;

  if (buff)
  { // Read nd bytes from the input strem
    memcpy(buff, dev->membuff + dev->bufptr, nd);
    dev->bufptr += nd;
    return nd; // Returns number of bytes read
  }
  else
  { // Remove nd bytes from the input stream
    dev->bufptr += nd;
    return nd;
  }
}

// User defined call-back function to output RGB bitmap to display device
//----------------------
static UINT tjd_output(
    JDEC *jd,     // Decompression object of current session
    void *bitmap, // Bitmap data to be output
    JRECT *rect   // Rectangular region to output
)
{
  BYTE *src = (BYTE *)bitmap;
  // Serial.printf("%d, %d, %d, %d\n", rect->top, rect->left, rect->bottom, rect->right);
  for (int y = rect->top; y <= rect->bottom; y++)
  {
    for (int x = rect->left; x <= rect->right; x++)
    {
      preview[y * 200 + x] = gfx->color565(*(src++), *(src++), *(src++));
    }
  }
  return 1; // Continue to decompression
}

void decodeJpegBuff(uint8_t arrayname[], uint32_t array_size, uint8_t scale)
{
  JDEC jd; // Decompression object (70 bytes)
  JRESULT rc;

  // dev.fhndl = NULL;
  // image from buffer
  dev.membuff = arrayname;
  dev.bufsize = array_size;
  dev.bufptr = 0;

  if (scale > 3)
    scale = 3;

  if (work)
  {
    rc = jd_prepare(&jd, tjd_buf_input, (void *)work, WORK_BUF_SIZE, &dev);
    if (rc == JDR_OK)
    {
      // Start to decode the JPEG file
      rc = jd_decomp(&jd, tjd_output, scale);
    }
  }
}

void decodeJpegFile(char filename[], uint8_t scale)
{
  JDEC jd; // Decompression object (70 bytes)
  JRESULT rc;

  dev.f = SD.open(filename);
  // image from buffer
  // dev.membuff = null;
  dev.bufsize = JPG_IMAGE_LINE_BUF_SIZE;
  dev.bufptr = 0;

  if (scale > 3)
    scale = 3;

  if (work)
  {
    rc = jd_prepare(&jd, tjd_file_input, (void *)work, WORK_BUF_SIZE, &dev);
    if (rc == JDR_OK)
    {
      // Start to decode the JPEG file
      rc = jd_decomp(&jd, tjd_output, scale);
    }
  }
}
