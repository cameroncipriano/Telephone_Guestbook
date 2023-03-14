// #include <freertos/FreeRTOS.h>
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

#include "SDCard.h"

static const char* TAG = "SDC";

#define SPI_DMA_CHAN 1

SDCard::SDCard(const char* mount_point, gpio_num_t miso, gpio_num_t mosi, gpio_num_t clk, gpio_num_t cs) {
  m_mount_point = mount_point;
  esp_err_t ret;
  // Options for mounting the filesystem.
  // If format_if_mount_failed is set to true, SD card will be partitioned and
  // formatted in case when mounting fails.
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 5,
      .allocation_unit_size = 16 * 1024 };

  Serial.println("Initializing SD card");

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  spi_bus_config_t bus_cfg = {
      .mosi_io_num = mosi,
      .miso_io_num = miso,
      .sclk_io_num = clk,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 4000,
  };
  sdspi_device_config_t device_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  device_config.host_id = (spi_host_device_t)host.slot;
  device_config.gpio_cs = cs;

  ret = spi_bus_initialize(device_config.host_id, &bus_cfg, SPI_DMA_CHAN);


  Serial.println("Calling mount of filesystem");
  ret = esp_vfs_fat_sdspi_mount(m_mount_point.c_str(), &host, &device_config, &mount_config, &m_card);

  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE(TAG, "Failed to mount filesystem. "
        "If you want the card to be formatted, set the format_if_mount_failed");
    }
    else {
      ESP_LOGE(TAG, "Failed to initialize the card (%s). "
        "Make sure SD card lines have pull-up resistors in place.",
        esp_err_to_name(ret));
    }
    return;
  }
  Serial.printf("SDCard mounted at: %s\n", m_mount_point.c_str());

  // Card has been initialized, print its properties
  sdmmc_card_print_info(stdout, m_card);
}

SDCard::~SDCard() {
  // All done, unmount partition and disable SDMMC or SPI peripheral
  esp_vfs_fat_sdmmc_unmount();
  Serial.println("Card unmounted");
}