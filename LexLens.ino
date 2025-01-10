// LexLens: Wifi Enabled ESP32-WROVER Translator Smart Glasses

#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "esp_camera.h"
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  21
#define SIOD_GPIO_NUM  26
#define SIOC_GPIO_NUM  27

#define Y9_GPIO_NUM    35
#define Y8_GPIO_NUM    34
#define Y7_GPIO_NUM    39
#define Y6_GPIO_NUM    36
#define Y5_GPIO_NUM    19
#define Y4_GPIO_NUM    18
#define Y3_GPIO_NUM    5
#define Y2_GPIO_NUM    4
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM  23
#define PCLK_GPIO_NUM  22

const char* ssid     = "***";
const char* password = "***";
const char* serverName = "http://***:5000/upload";

//oled SPI
#define OLED_MOSI   13  //din
#define OLED_SCLK   14  //clk
#define OLED_CS     15  //cs
#define OLED_DC     2   //dc
#define OLED_RESET  12  //rst

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

// init OLED
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT,
                         &SPI, OLED_DC, OLED_RESET, OLED_CS, 8000000);

unsigned long lastCaptureTime = 0;
const unsigned long captureInterval = 5000;

// display timing
unsigned long displayStartTime = 0;
bool isDisplaying = false;
int currentDisplayLine = 0;

// display buffer
const int maxDisplayLines = 6;
String displayLines[maxDisplayLines];
int totalDisplayLines = 0;

//server
#include <esp_http_server.h>
#include <esp_timer.h>
#include <img_converters.h>
#include <fb_gfx.h>
#include <esp32-hal-ledc.h>
#include <sdkconfig.h>
#include "camera_index.h"

httpd_handle_t camera_httpd = NULL;

// capture + send as jpeg
static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;

  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  // send image buffer
  res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);

  if (res != ESP_OK) {
    Serial.println("Failed to send image");
    return res;
  }

  return res;
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 4;

  // handler for image capture
  httpd_uri_t capture_uri = {
    .uri = "/capture",
    .method = HTTP_GET,
    .handler = capture_handler,
    .user_ctx = NULL
  };

  Serial.println("Starting web server...");
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    Serial.println("Web server started successfully.");
  } else {
    Serial.println("Failed to start web server!");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nStarting LexLens...");

  // wifi connect
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  initCamera();
  initDisplay();
  display.setRotation(2); // 2 = 180 deg 

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Smart Glasses");
  display.println("Initializing...");
  display.display();

  startCameraServer();

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Smart Glasses Ready");
  display.println("IP: " + String(WiFi.localIP()));
  display.display();
}

void loop() {
  unsigned long currentTime = millis();
  
  // check if its time to capture a new image and not currently displaying text
  if (currentTime - lastCaptureTime >= captureInterval && !isDisplaying) {
    lastCaptureTime = currentTime;
    captureAndSendImage();
  }
  
  // display timing
  if (isDisplaying) {
    if (currentTime - displayStartTime >= 1000) { // 1 second per line, adjust as needed
      displayStartTime = currentTime;
      // display  next line
      if (currentDisplayLine < totalDisplayLines) {
        // clear the specific line area
        display.fillRect(0, currentDisplayLine * 10, SCREEN_WIDTH, 10, BLACK);
        
        display.setCursor(0, currentDisplayLine * 10);
        display.println(displayLines[currentDisplayLine]);
        display.display();
        
        currentDisplayLine++;
      } else {
        // finished displaying all lines
        isDisplaying = false;
        currentDisplayLine = 0;
        
        display.clearDisplay();
        display.display();
      }
    }
  }
}

void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;

  config.xclk_freq_hz = 10000000; // lower to 10 MHz to save resources, high fps is not needed

  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size   = FRAMESIZE_SXGA; // 1280x1024
    config.jpeg_quality = 4;            // lower = better quality
    config.fb_count     = 2;
  } else {
    config.frame_size   = FRAMESIZE_XGA;  // 1024x768
    config.jpeg_quality = 4;
    config.fb_count     = 2;
  }

  // init camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    while (true) { delay(1000); } // halt if camera fails
  }

  Serial.println("Camera initialized (WROVER KIT).");

  // access camera sensor to adjust settings
  sensor_t *s = esp_camera_sensor_get();
  if (s != nullptr) {

    // set gain ceiling to minimum (GAINCEILING_2X corresponds to 2x gain)
    s->set_gainceiling(s, GAINCEILING_2X);
    Serial.println("Camera settings adjusted: Gain ceiling set to minimum.");
  } else {
    Serial.println("Failed to get camera sensor.");
  }
}

void initDisplay() {
  // init SPI bus (MISO = -1 bc the OLED is write-only)
  SPI.begin(OLED_SCLK, -1, OLED_MOSI, OLED_CS);

  // init display
  if (!display.begin(SSD1306_SWITCHCAPVCC)) {
    Serial.println("SSD1306 init failed!");
    while (true) { delay(1000); }
  }
  display.clearDisplay();
  display.display();
  delay(500);
  Serial.println("OLED display initialized.");
}

void captureAndSendImage() {
  Serial.println("\nCapturing image...");
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed!");
    return;
  }
  Serial.printf("Captured %d bytes\n", fb->len);

  //http post
  HTTPClient http;
  http.begin(serverName);
  http.addHeader("Content-Type", "image/jpeg");

  Serial.println("Sending image to Flask server...");
  int httpResponseCode = http.POST(fb->buf, fb->len);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.print("Server response: ");
    Serial.println(response);

    // parse translated_text from JSON
    String translatedText = parseJSONTranslatedText(response);
    Serial.println("Parsed text: " + translatedText);

    prepareDisplayLines(translatedText);

    isDisplaying = true;
    displayStartTime = millis();
  } else {
    String error = "POST failed: " + String(httpResponseCode);
    Serial.println(error);
    displayError(error);
  }

  http.end();
  esp_camera_fb_return(fb); // release the buffer
}

void prepareDisplayLines(String text) {
  // remove newline characters from appearing
  text.replace("\n", " ");
  
  // max characters per line
  const int maxCharsPerLine = 21;
  int currentIndex = 0;
  int textLength = text.length();
  
  // reset display lines
  totalDisplayLines = 0;
  
  while (currentIndex < textLength && totalDisplayLines < maxDisplayLines) {
    // determine end index for the current line
    int endIndex = currentIndex + maxCharsPerLine;
    if (endIndex >= textLength) {
      endIndex = textLength;
    } else {
      // find the last space within the maxCharsPerLine to avoid splitting words
      int lastSpace = text.lastIndexOf(' ', endIndex);
      if (lastSpace > currentIndex) {
        endIndex = lastSpace;
      }
    }
    
    // extract the line
    String line = text.substring(currentIndex, endIndex);
    currentIndex = endIndex;
    if (currentIndex < textLength && text.charAt(currentIndex) == ' ') {
      currentIndex++;
    }
    
    // store the line in the buffer
    displayLines[totalDisplayLines++] = line;
  }
}

//JSON parsing
String parseJSONTranslatedText(const String &json) {
  int keyPos = json.indexOf("\"translated_text\"");
  if (keyPos == -1) {
    return "No translated text";
  }
  int colonPos = json.indexOf(":", keyPos);
  if (colonPos == -1) {
    return "Malformed JSON";
  }
  int quoteStart = json.indexOf("\"", colonPos);
  if (quoteStart == -1) {
    return "Malformed JSON";
  }
  int quoteEnd = json.indexOf("\"", quoteStart + 1);
  if (quoteEnd == -1) {
    return "Malformed JSON";
  }
  return json.substring(quoteStart + 1, quoteEnd);
}

//display error message
void displayError(String errorMsg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Error:");
  display.println(errorMsg);
  display.display();
  delay(3000); // Display for 3 seconds
  display.clearDisplay();
  display.display();
}

//display translated text
void displayTranslatedText(String text) {
  // Remove any '\n' characters
  text.replace("\n", " ");
  
  // wrao text into lines of max 21 characters without splitting words
  const int maxCharsPerLine = 21;
  int currentIndex = 0;
  int textLength = text.length();
  
    display.clearDisplay();
  display.setTextSize(1);       // 6x8 font
  display.setTextColor(WHITE);
  
  // calc the max number of lines that can fit on the display
  const int maxDisplayLines = SCREEN_HEIGHT / 10; // 64 / 10 = 6 lines
  int displayedLines = 0;
  
  while (currentIndex < textLength && displayedLines < maxDisplayLines) {
    // determine the end index for the current line
    int endIndex = currentIndex + maxCharsPerLine;
    if (endIndex >= textLength) {
      endIndex = textLength;
    } else {
      // find the last space within the maxCharsPerLine to avoid splitting words
      int lastSpace = text.lastIndexOf(' ', endIndex);
      if (lastSpace > currentIndex) {
        endIndex = lastSpace;
      }
    }
    
    // extract the line
    String line = text.substring(currentIndex, endIndex);
    currentIndex = endIndex;
    if (currentIndex < textLength && text.charAt(currentIndex) == ' ') {
      currentIndex++;
    }
    
    // calc Y position with spacing
    int cursorY = displayedLines * 10; // 8 pixels font height + 2 pixels spacing
    if (cursorY + 8 > SCREEN_HEIGHT) {
      break; // Prevent overflow
    }
    
    display.setCursor(0, cursorY);
    display.println(line);
    displayedLines++;
  }
  
  // update display once with all lines
  display.display();
  
  // scrolling delay based on the number of lines, adjust for readability
  const int delayPerLine = 1000; // 1 sec per line
  delay(displayedLines * delayPerLine);
  
  // clear the display after showing the text
  display.clearDisplay();
  display.display();
}
