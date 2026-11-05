/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : TPMS simulator (balloon model) - 4 wheel nodes + dashboard
  *
  * ADC1 (scan + DMA, circular) channels:
  *   Rank 1 : IN0  (PA0)  -> FL pressure  (MPX5010DP via 10k/20k divider)
  *   Rank 2 : IN1  (PA1)  -> FR pressure
  *   Rank 3 : IN4  (PA4)  -> RL pressure
  *   Rank 4 : IN8  (PB0)  -> RR pressure
  *   Rank 5 : IN10 (PC0)  -> LM35 temperature (powered from 5 V)
  *
  * Outputs:
  *   PB4..PB7 : alert LEDs FL, FR, RL, RR
  *   PB8      : buzzer
  *   USART2   : 115200 baud, lines like  FL,2.85,27.3,NORMAL
  *
  * FIXES APPLIED (see inline comments marked "FIX:"):
  *   1. MX_DMA_Init() now actually initializes hdma_adc1 and links it to
  *      hadc1 via __HAL_LINKDMA(). Previously the DMA stream was never
  *      configured, so HAL_ADC_Start_DMA() had nothing to run on.
  *   2. MX_GPIO_Init() now sets PA0/PA1/PA4, PB0 and PC0 to GPIO_MODE_ANALOG.
  *      Previously these ADC input pins were left in their reset state.
  *   3. MX_GPIO_Init() now configures PA2/PA3 as USART2 alternate function
  *      (AF7) pins. Previously USART2 TX/RX were never routed to the pins.
  *   4. Minor: USER CODE marker placement around the main while(1) loop
  *      corrected to match standard CubeMX template ordering.
  *
  * NOTE: Peripheral clock enables for ADC1 and USART2 themselves
  * (__HAL_RCC_ADC1_CLK_ENABLE / __HAL_RCC_USART2_CLK_ENABLE) are expected
  * inside HAL_ADC_MspInit() / HAL_UART_MspInit() in stm32f4xx_hal_msp.c,
  * which is not part of this file. If that file is missing those calls,
  * add them there.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define P_LOW          1.5f      // kPa  : below this  -> LOW
#define P_HIGH         4.5f      // kPa  : above this  -> HIGH
#define T_HIGH         45.0f     // degC : above this  -> OVERHEAT
#define KPA_PER_COUNT  0.002686f // MPX5010DP behind 10k/20k divider
#define ADC_MIN_VALID  50.0f     // raw below this -> sensor disconnected
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
volatile uint16_t adc_buf[5];            // filled by DMA: FL, FR, RL, RR, TEMP
float raw0[4] = {0};                     // zero-pressure offsets (calibrated at startup)
const char *wheel[4] = {"FL", "FR", "RL", "RR"};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
float adc_to_kpa(float raw, float zero)
{
    float p = (raw - zero) * KPA_PER_COUNT;
    return (p < 0.0f) ? 0.0f : p;
}

float adc_to_celsius(float raw)
{
    return (raw * 3.3f / 4095.0f) * 100.0f;   // LM35: 10 mV per degC
}

const char* check_status(float raw, float p, float t)
{
    if (raw < ADC_MIN_VALID) return "NO SIGNAL";
    if (p < P_LOW)           return "LOW";
    if (p > P_HIGH)          return "HIGH";
    if (t > T_HIGH)          return "OVERHEAT";
    return "NORMAL";
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_buf, 5);

  /* Zero-pressure calibration: keep balloons DETACHED (tubing open to air)
     during the first second after reset. */
  HAL_Delay(500);                                   // let the ADC settle
  for (int s = 0; s < 32; s++)
  {
      for (int i = 0; i < 4; i++) raw0[i] += adc_buf[i];
      HAL_Delay(5);
  }
  for (int i = 0; i < 4; i++) raw0[i] /= 32.0f;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    float avg[5] = {0};
    for (int s = 0; s < 16; s++)                    // average 16 samples (noise reduction)
    {
        for (int i = 0; i < 5; i++) avg[i] += adc_buf[i];
        HAL_Delay(5);
    }
    for (int i = 0; i < 5; i++) avg[i] /= 16.0f;

    float temp = adc_to_celsius(avg[4]);
    uint8_t alarm = 0;
    char msg[64];

    for (int i = 0; i < 4; i++)
    {
        float p = adc_to_kpa(avg[i], raw0[i]);
        const char *st = check_status(avg[i], p, temp);
        uint8_t fault = (strcmp(st, "NORMAL") != 0);
        alarm |= fault;

        HAL_GPIO_WritePin(GPIOB, (uint16_t)(GPIO_PIN_4 << i),
                          fault ? GPIO_PIN_SET : GPIO_PIN_RESET);

        int len = snprintf(msg, sizeof(msg), "%s,%.2f,%.1f,%s\r\n",
                           wheel[i], (double)p, (double)temp, st);

        /* Guard against a truncated/failed snprintf so we never pass a
           negative or out-of-range length to HAL_UART_Transmit. */
        if (len < 0)
        {
            len = 0;
        }
        else if ((size_t)len >= sizeof(msg))
        {
            len = sizeof(msg) - 1;
        }

        HAL_UART_Transmit(&huart2, (uint8_t*)msg, len, 100);
    }
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, alarm ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_Delay(500);
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = ENABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 5;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Rank 1 : Channel 0 (PA0) - FL pressure
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Rank 2 : Channel 1 (PA1) - FR pressure
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = 2;
  sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Rank 3 : Channel 4 (PA4) - RL pressure
  */
  sConfig.Channel = ADC_CHANNEL_4;
  sConfig.Rank = 3;
  sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Rank 4 : Channel 8 (PB0) - RR pressure
  */
  sConfig.Channel = ADC_CHANNEL_8;
  sConfig.Rank = 4;
  sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Rank 5 : Channel 10 (PC0) - LM35 temperature
  */
  sConfig.Channel = ADC_CHANNEL_10;
  sConfig.Rank = 5;
  sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* USER CODE BEGIN DMA_Init */
  /* FIX: hdma_adc1 was never actually initialized before - only the DMA2
     clock was enabled and the NVIC priority was set, but the stream itself
     had no configuration and was never linked to hadc1. ADC1's DMA request
     on this device maps to DMA2 Stream0 Channel 0. Without this block,
     HAL_ADC_Start_DMA() has no functioning stream to run on and adc_buf[]
     is never actually filled. */
  hdma_adc1.Instance = DMA2_Stream0;
  hdma_adc1.Init.Channel             = DMA_CHANNEL_0;
  hdma_adc1.Init.Direction           = DMA_PERIPH_TO_MEMORY;
  hdma_adc1.Init.PeriphInc           = DMA_PINC_DISABLE;
  hdma_adc1.Init.MemInc              = DMA_MINC_ENABLE;
  hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  hdma_adc1.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
  hdma_adc1.Init.Mode                = DMA_CIRCULAR;
  hdma_adc1.Init.Priority            = DMA_PRIORITY_LOW;
  hdma_adc1.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
  if (HAL_DMA_Init(&hdma_adc1) != HAL_OK)
  {
    Error_Handler();
  }

  __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);
  /* USER CODE END DMA_Init */

  /* DMA interrupt init */
  /* DMA2_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7
                          |GPIO_PIN_8, GPIO_PIN_RESET);

  /* FIX: PA0, PA1, PA4 (FL/FR/RL pressure) were never configured as analog
     inputs - they were left in their GPIO reset state, which would make
     the corresponding ADC channels read garbage. */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* FIX: PB0 (RR pressure) likewise never configured as analog input. */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* FIX: PC0 (LM35 temperature) likewise never configured as analog input. */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* FIX: PA2 (USART2_TX) / PA3 (USART2_RX) were never routed to the
     USART2 alternate function - without this, no serial output is
     physically possible regardless of what MX_USART2_UART_Init() does. */
  GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PB4 PB5 PB6 PB7
                           PB8 */
  GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7
                          |GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
