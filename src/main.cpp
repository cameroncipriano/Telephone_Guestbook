#include <Arduino.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include <I2SMEMSSampler.h>
#include <ADCSampler.h>
#include <I2SOutput.h>
#include <DACOutput.h>
#include <SDCard.h>
#include <WAVFileReader.h>
#include <WAVFileWriter.h>
#include <Bounce2.h>
#include "config.h"


// Enums for state operation
enum ErrorState { MOUNT_FAILURE, NO_SD_CARD };
enum Mode { INITIALIZING, READY, PROMPTING, RECORDING };

// Local State
Mode current_mode = Mode::INITIALIZING;

I2SSampler* input = nullptr;
Output* output = nullptr;

Bounce2::Button hook_switch = Bounce2::Button();
int16_t file_number = 1;

static const char* TAG = "MAIN";

// [void wait_for_button_push() {
//   while (gpio_get_level(HOOK_SWITCH) == 0) {
//     vTaskDelay(pdMS_TO_TICKS(100));
//   }
// }]

void wait(unsigned int delayMillis) {
  Serial.println("********    Waiting to prompt    ********");
  unsigned long startWaitTime = millis();
  while (millis() - startWaitTime <= delayMillis) {
    hook_switch.update();
  }
  Serial.println("Done waiting...");
}

void play(Output* output, const char* fname) {
  int16_t* samples = (int16_t*)malloc(sizeof(int16_t) * 1024);
  // open the file on the sdcard
  FILE* fp = fopen(fname, "rb");
  // create a new wave file writer
  WAVFileReader* reader = new WAVFileReader(fp);
  Serial.println("********    Start playing    ********");
  output->start(reader->sample_rate());
  Serial.println("Opened wav file");
  // read until theres no more samples
  while (true) {
    hook_switch.update();
    if (hook_switch.rose()) {
      Serial.println("********    Handset placed back during prompt    ********");
      current_mode = Mode::READY;
      break;
    }
    int samples_read = reader->read(samples, 1024);
    if (samples_read == 0) {
      break;
    }
    Serial.printf("Read %d samples\n", samples_read);
    output->write(samples, samples_read);
    Serial.println("Played samples");
  }
  // stop the input
  output->stop();
  fclose(fp);
  delete reader;
  free(samples);
  Serial.println("********    Finished playing    ********");
}

void record(I2SSampler* input, const char* fname) {
  int16_t* samples = (int16_t*)malloc(sizeof(int16_t) * 1024);
  Serial.println("********    Start recording    ********");
  input->start();
  // open the file on the sdcard
  FILE* fp = fopen(fname, "wb");
  // create a new wave file writer
  WAVFileWriter* writer = new WAVFileWriter(fp, input->sample_rate());
  // keep writing until the user releases the button
  while (!hook_switch.rose()) {
    int samples_read = input->read(samples, 1024);
    int64_t start = esp_timer_get_time();
    writer->write(samples, samples_read);
    int64_t end = esp_timer_get_time();
    Serial.printf("Wrote %d samples in %lld microseconds\n", samples_read, end - start);
    hook_switch.update();
  }
  // stop the input
  input->stop();
  // and finish the writing
  writer->finish();
  fclose(fp);
  delete writer;
  free(samples);

  // play what we just recorded to test
  // play(output, fname);

  Serial.println("********    Finished recording     ********");
}

void main_task(void* param) {
  while (true) {
    // wait for the user to push and hold the button
    hook_switch.update();
    switch (current_mode) {
      case Mode::READY:
        if (hook_switch.fell()) {
          Serial.println("********    Handset was lifted!    ********");
          current_mode = Mode::PROMPTING;
        }
        break;
      case Mode::PROMPTING:
        Serial.println("********    Prompting    ********");
        wait(300); // allow the user to bring the headset to their ear
        if (hook_switch.isPressed()) {
          current_mode = Mode::READY;
          continue;
        }
        play(output, "/sdcard/prompt_mono_10dB.wav");
        current_mode = Mode::RECORDING;
        break;
      case Mode::RECORDING:
        Serial.println("********    Recording    ********");
        char fname[256];
        sprintf(fname, "/sdcard/%d_%d.wav", file_number, esp_timer_get_time());
        file_number++;

        record(input, fname);
        current_mode = Mode::READY;
        break;
      case Mode::INITIALIZING:
        break;
    }
    // vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void setup() {
  Serial.begin(115200);

  Serial.println("Starting up");
  Serial.println("Mounting SDCard on /sdcard");
  new SDCard("/sdcard", PIN_NUM_MISO, PIN_NUM_MOSI, PIN_NUM_CLK, PIN_NUM_CS);

  Serial.println("Creating microphone");
  input = new ADCSampler(ADC_UNIT_1, ADC1_CHANNEL_7, i2s_adc_config);

  Serial.println("Creating output speaker");
  output = new I2SOutput(I2S_NUM_0, i2s_speaker_pins);

  Serial.println("Creating Hook Switch");
  hook_switch.attach(HOOK_SWITCH, INPUT_PULLUP);
  hook_switch.interval(40);
  hook_switch.setPressedState(HIGH);
  current_mode = Mode::READY;
  Serial.println("Setting up");

  xTaskCreate(main_task, "Main", 4096, NULL, 0, NULL);
}

void loop() {
}