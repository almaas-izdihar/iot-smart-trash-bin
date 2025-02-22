// Board: AI Thinker ESP32-CAM

#include "esp_camera.h" // default from board

#include <ESP32Servo.h> // lib: ESP32Servo by Kevin Harrington,John K. Bennet
#include <Arduino_JSON.h> // lib: Arduino_JSON by Arduino

#include <TensorFlowLite_ESP32.h> // lib: TensorFlowLite_ESP32 by TensorFlow Authors
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "waste_model_8.h"

#define CAMERA_MODEL_AI_THINKER

// Servo motor configuration
Servo myservo;
const int servoPin = 14; // Change to your servo pin
const int leftPosition = 0;
const int rightPosition = 180;

// TENSORFLOW LITE MODEL

class DDTFLErrorReporter : public tflite::ErrorReporter {
public:
  virtual int Report(const char* format, va_list args) {
    int len = strlen(format);
    char buffer[max(32, 2 * len)];  // assume 2 times format len is big enough
    vsnprintf(buffer, sizeof(buffer), format, args);
    // Print to serial monitor instead of dumbdisplay.writeComment
    Serial.print("TF Lite Error: ");
    Serial.println(buffer);
    return 0;
  }
};

tflite::ErrorReporter* error_reporter = new DDTFLErrorReporter();
const tflite::Model* model = ::tflite::GetModel(waste_model_8_tflite);
const int tensor_arena_size = 107 * 1024;
uint8_t* tensor_arena;

tflite::MicroInterpreter* interpreter = NULL;
TfLiteTensor* input;

// CAMERA

const framesize_t FrameSize = FRAMESIZE_96X96;        // should agree with kNumCols and kNumRows
const pixformat_t PixelFormat = PIXFORMAT_GRAYSCALE;  // should be grayscale
bool initialiseCamera();
bool cameraReady;

int cameraImageBrightness = 0;                       // Image brightness (-2 to +2)

const int brightLED = 4;                             // onboard Illumination/flash LED pin (4)
const int ledFreq = 5000;                            // PWM settings
const int ledChannel = 15;                           // camera uses timer1
const int ledRresolution = 8;                        // resolution (8 = from 0 to 255)

#define PWDN_GPIO_NUM     32      // power to camera (on/off)
#define RESET_GPIO_NUM    -1      // -1 = not used
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26      // i2c sda
#define SIOC_GPIO_NUM     27      // i2c scl
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25      // vsync_pin
#define HREF_GPIO_NUM     23      // href_pin
#define PCLK_GPIO_NUM     22      // pixel_clock_pin

void brightLed(byte ledBrightness){
  ledcWrite(ledChannel, ledBrightness);   // change LED brightness (0 - 255)
}

void setupFlashPWM() {
  ledcSetup(ledChannel, ledFreq, ledRresolution);
  ledcAttachPin(brightLED, ledChannel);
  brightLed(32);
  brightLed(0);
}

bool cameraImageSettings() {
  sensor_t *s = esp_camera_sensor_get();
  if (s == NULL) {
    return 0;
  }

  // enable auto adjust
  s->set_gain_ctrl(s, 1);                       // auto gain on
  s->set_exposure_ctrl(s, 1);                   // auto exposure on
  s->set_awb_gain(s, 1);                        // Auto White Balance enable (0 or 1)
  s->set_brightness(s, cameraImageBrightness);  // (-2 to 2) - set brightness

  return 1;
}

bool initialiseCamera() {
  esp_camera_deinit();     // disable camera
  delay(50);
  setupFlashPWM();         // configure PWM for the illumination LED

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
  config.xclk_freq_hz = 20000000;               // XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
  config.pixel_format = PixelFormat;            // Options =  YUV422, GRAYSCALE, RGB565, JPEG, RGB888
  config.frame_size = FrameSize;                // Image sizes: 160x120 (QQVGA), 128x160 (QQVGA2), 176x144 (QCIF), 240x176 (HQVGA), 320x240 (QVGA),
                                                //              400x296 (CIF), 640x480 (VGA, default), 800x600 (SVGA), 1024x768 (XGA), 1280x1024 (SXGA),
                                                //              1600x1200 (UXGA)
  config.jpeg_quality = 15;                     // 0-63 lower number means higher quality
  config.fb_count = 1;                          // if more than one, i2s runs in continuous mode. Use only with JPEG

  // check the esp32cam board has a psram chip installed (extra memory used for storing captured images)
  //    Note: if not using "AI thinker esp32 cam" in the Arduino IDE, SPIFFS must be enabled
  if (!psramFound()) {
    error_reporter->Report("Warning: No PSRam found so defaulting to image size 'CIF'");
    config.frame_size = FRAMESIZE_CIF;
  }

  esp_err_t camerr = esp_camera_init(&config);  // initialise the camera
  if (camerr != ESP_OK) {
    error_reporter->Report("ERROR: Camera init failed with error 0x%x", camerr);
  }

  cameraImageSettings();                        // apply custom camera settings

  return (camerr == ESP_OK);                    // return boolean result of camera initialisation
}

// MAIN

void setup() {
  Serial.begin(115200);
  Serial.println("begin setup");

  // Attach the servo on the pin
  myservo.attach(servoPin);

  // Check version to make sure supported
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    error_reporter->Report("Model provided is schema version %d not equal to supported version %d.", model->version(), TFLITE_SCHEMA_VERSION);
    return;
  }

  // Allocate memory for tensor_arena
  tensor_arena = (uint8_t *)heap_caps_malloc(tensor_arena_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (tensor_arena == NULL) {
    error_reporter->Report("heap_caps_malloc() failed");
    return;
  }

  // Pull in only the operation implementations needed
  tflite::MicroMutableOpResolver<7>* micro_op_resolver = new tflite::MicroMutableOpResolver<7>();
  micro_op_resolver->AddConv2D();
  micro_op_resolver->AddMaxPool2D();
  micro_op_resolver->AddMean();
  micro_op_resolver->AddFullyConnected();
  micro_op_resolver->AddLogistic();

  // Build an interpreter to run the model with
  interpreter = new tflite::MicroInterpreter(model, *micro_op_resolver, tensor_arena, tensor_arena_size, error_reporter);

  // Allocate memory from the tensor_arena for the model's tensors
  TfLiteStatus allocate_status = interpreter->AllocateTensors();
  if (allocate_status != kTfLiteOk) {
    error_reporter->Report("AllocateTensors() failed");
    return;
  }

  // Obtain a pointer to the model's input tensor
  input = interpreter->input(0);

  Serial.println("Done preparing TFLite model!");

  cameraReady = initialiseCamera(); 
  if (cameraReady) {
    Serial.println("Initialized camera!");
  } else {
    Serial.println("Failed to initialize camera!");
  }

  Serial.println("setup completed");
}

float sigmoid(float x) {
  return 1 / (1 + exp(-x));
}

void printInputTensorDetails(TfLiteTensor* input) {
  Serial.print("Input tensor name: ");
  Serial.println(input->name);

  Serial.print("Input tensor shape: [");
  for (int i = 0; i < input->dims->size; i++) {
    Serial.print(input->dims->data[i]);
    if (i < input->dims->size - 1) {
      Serial.print(", ");
    }
  }
  Serial.println("]");
}

void loop() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  // Resize and convert the image to match the model's input shape
  uint8_t* resized_img = (uint8_t*)malloc(48 * 48 * 3);
  if (resized_img == NULL) {
    Serial.println("Failed to allocate memory for resized image");
    esp_camera_fb_return(fb);
    return;
  }

  // Assuming the original image is 96x96 and grayscale
  // Resize the image to 48x48 and convert to 3 channels
  for (int y = 0; y < 48; y++) {
    for (int x = 0; x < 48; x++) {
      int orig_x = x * 2;
      int orig_y = y * 2;
      uint8_t pixel_value = fb->buf[orig_y * 96 + orig_x]; // Access grayscale pixel

      // Assign the pixel value to the 3 channels (R, G, B)
      resized_img[(y * 48 + x) * 3 + 0] = pixel_value;
      resized_img[(y * 48 + x) * 3 + 1] = pixel_value;
      resized_img[(y * 48 + x) * 3 + 2] = pixel_value;
    }
  }

  // Normalize the pixel values to float32 range [0, 1]
  for (int i = 0; i < 48 * 48 * 3; i++) {
    input->data.f[i] = resized_img[i] / 255.0;
  }

  free(resized_img);

  // Print input tensor shape and data
  Serial.println("Input tensor:");
  printInputTensorDetails(input);
  Serial.println("Data:");
  for (int i = 0; i < 10; i++) { // Print first 10 values for brevity
    Serial.print(input->data.f[i]);
    Serial.print(" ");
  }
  Serial.println();

  // Run inference
  TfLiteStatus invoke_status = interpreter->Invoke();
  if (invoke_status != kTfLiteOk) {
    Serial.println("Inference failed");
    esp_camera_fb_return(fb);
    return;
  }

  // Get the output tensor
  TfLiteTensor* output = interpreter->output(0);

  // Print output tensor information
  Serial.print("Output tensor name: ");
  Serial.println(output->name);

  Serial.print("Output tensor shape: [");
  for (int i = 0; i < output->dims->size; i++) {
    Serial.print(output->dims->data[i]);
    if (i < output->dims->size - 1) {
      Serial.print(", ");
    }
  }
  Serial.println("]");

  // Get the output value
  float output_value = output->data.f[0];
  Serial.print("Output value: ");
  Serial.println(output_value);

  // Control the servo motor based on the output value
  if (output_value < 0.5) {
    myservo.write(leftPosition); // Rotate servo to left position
    Serial.println("Servo rotating to left (Organic)");
  } else {
    myservo.write(rightPosition); // Rotate servo to right position
    Serial.println("Servo rotating to right (Non-Organic)");
  }

  esp_camera_fb_return(fb);

  delay(1000); // Optional delay
}
