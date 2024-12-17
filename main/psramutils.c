#include "main.h"

static const char *TAG = "BeyBlades Psram";
static bool haspsram = false;

size_t getFreePsram(void)
{
  return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

uint32_t getFreeHeap(void)
{
  return heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}

void initPsram(void)
{
  if (esp_psram_is_initialized() == false)
  {
    switch (esp_psram_init())
    {
    case ESP_OK:
      ESP_LOGI(TAG, "PsRam found size %d", esp_psram_get_size());
      haspsram = true;
      break;
    case ESP_FAIL:
      ESP_LOGI(TAG, "PsRam not found");
      haspsram = false;
      break;
    case ESP_ERR_INVALID_STATE:
      ESP_LOGI(TAG, "PsRam already initialized size %d", esp_psram_get_size());
      break;
    }
  } else {
    ESP_LOGI(TAG, "PsRam already initialized size %d", esp_psram_get_size());
    haspsram = true;
  }
}

bool psramFound(void){
  return haspsram;
}

char *allocatePSRAM(size_t aSize)
{
  //if (psramFound() && getFreePsram() > aSize)
  // {
  //   return (char *)heap_caps_malloc(aSize,MALLOC_CAP_SPIRAM);
  // }
  return NULL;
}

char *allocateMemory(char *aPtr, size_t aSize, bool fail, bool psramOnly)
{

  //  Since current buffer is too smal, free it
  if (aPtr != NULL)
  {
    free(aPtr);
    aPtr = NULL;
  }

  char *ptr = NULL;

  if (psramOnly)
  {
    ptr = allocatePSRAM(aSize);
  }
  else
  {
    // If memory requested is more than 2/3 of the currently free heap, try PSRAM immediately
    if (aSize > getFreeHeap() * 2 / 3)
    {
      ptr = allocatePSRAM(aSize);
    }
    else
    {
      //  Enough free heap - let's try allocating fast RAM as a buffer
      ptr = (char *)malloc(aSize);

      //  If allocation on the heap failed, let's give PSRAM one more chance:
      if (ptr == NULL)
        ptr = allocatePSRAM(aSize);
    }
  }
  // Finally, if the memory pointer is NULL, we were not able to allocate any memory, and that is a terminal condition.
  if (fail && ptr == NULL)
  {
    ESP_LOGE(TAG, "allocateMemory: Out of memory!");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
  }
  return ptr;
}
